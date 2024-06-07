// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "runtime_utils.hpp"
#include "runtime_types.hpp"
#include "runtime_workload.hpp"
#include "common/mem_lib.hpp"
#include "common/size_lib.hpp"
#include "common/buda_soc_descriptor.h"
#include "common/model/tt_core.hpp"
#include "epoch_loader.hpp"
#include "fmt/ostream.h"
#include <cmath>
#include <functional>
// ---------------------------------------------------------------------------
// Workload w/ extra capabilities
// ---------------------------------------------------------------------------

void tt_runtime_queue_ptrs_wrap::clear() {
    pending_queue_settings.clear();
    pending_varinst_queue_updates.clear();

    for (auto &field_type : supported_fields) {
        header_field_queue_to_var_map[field_type] = {};
        header_field_vars_to_queue_map[field_type] = {};
    }
}

void tt_runtime_queue_ptrs_wrap::update_pending(string q_name, bool read_only) {
    // An existing pending update is stale and is overwritten
    pending_queue_settings[q_name] = tt_queue_setting_info{
        .name = q_name,
        .zero = header_field_queue_to_var_map[tt_queue_header_field::ZeroSetting].at(q_name),
        .rd_ptr_local = header_field_queue_to_var_map[tt_queue_header_field::GlobalRdptr].at(q_name), // FIXME - Should it be modified to point to lptr?
        .rd_ptr_global = header_field_queue_to_var_map[tt_queue_header_field::GlobalRdptr].at(q_name),
        .wr_ptr_global = header_field_queue_to_var_map[tt_queue_header_field::GlobalWrptr].at(q_name),
        .read_only = (read_only ? "true" : "false"),
    };
}

void tt_runtime_queue_ptrs_wrap::erase_pending(string q_name) {
    pending_queue_settings.erase(q_name);
}

string tt_runtime_queue_ptrs_wrap::get_queue_field_var(tt_queue_header_field field_type, string q_name) {
    bool mapped = header_field_queue_to_var_map[field_type].find(q_name) != header_field_queue_to_var_map[field_type].end();
    if (mapped) {
        auto &var_name = header_field_queue_to_var_map[field_type].at(q_name);
        log_assert(is_field_type_var(field_type, var_name), "Incorrect type for {}", var_name);
        return var_name;
    }
    return "";
}

// Remove old mapping for Queue, and update new mappings for queue and Field type.
void tt_runtime_queue_ptrs_wrap::map_queue_field_var(tt_queue_header_field field_type, string q_name, string var_name) {
    string mapped_name = get_queue_field_var(field_type, q_name);
    if (is_field_type_var(field_type, mapped_name)) {
        header_field_vars_to_queue_map[field_type][mapped_name].erase(q_name);
    }
    header_field_queue_to_var_map[field_type][q_name] = var_name;
    header_field_vars_to_queue_map[field_type][var_name].insert(q_name);
}


const std::unordered_set<std::string>& tt_runtime_queue_ptrs_wrap::get_queue_set(tt_queue_header_field field_type, std::string var_name) const {
    log_assert(header_field_vars_to_queue_map.find(field_type) != header_field_vars_to_queue_map.end(),
        "{} -- field_type: {} doesn't exist in map.", __FUNCTION__, (int) field_type);
    log_assert(header_field_vars_to_queue_map.at(field_type).find(var_name) != header_field_vars_to_queue_map.at(field_type).end(),
        "{} -- var_name: {} doesn't exist for header_field: {} in map.", __FUNCTION__, var_name, (int) field_type);
    return header_field_vars_to_queue_map.at(field_type).at(var_name);
}


const std::vector<tt_queue_header_field> tt_runtime_queue_ptrs_wrap::get_var_field_types(const std::string &var_name) {
    std::vector<tt_queue_header_field> field_types;
    for (auto &field_type : supported_fields) {
        if (is_field_type_var(field_type, var_name)) {
            field_types.push_back(field_type);
        }
    }
    return field_types;
}

tt_runtime_workload::~tt_runtime_workload() {
    for (auto& memptr : allocated_tensor_memories) {
        deallocate_memory(memptr);
    }
}

netlist_program &tt_runtime_workload::get_program(string prog_name) {
    log_assert (programs.find(prog_name) != programs.end(), "Program being executed doesn't exist...");
    return programs.at(prog_name);
}

netlist_program &tt_runtime_workload::reset_program(string prog_name) {
    netlist_program &program = get_program(prog_name);
    program.reset();
    return program;
}

void tt_runtime_workload::check_program(string prog_name) {
    tt_runtime_queue_ptrs_wrap &qptrs_wrap = get_qptrs_wrap(prog_name);
    if (qptrs_wrap.pending_queue_settings.size()) {
        int num_pending = qptrs_wrap.pending_queue_settings.size();
        log_trace(LogRuntime, "Pending queue settings queue is non-empty with {} entries! Queues that span multiple programs may not end up in the correct state...", num_pending);
        for (auto &it : qptrs_wrap.pending_queue_settings) {
            log_trace(LogRuntime, " {} - {} ", it.first, it.second);
        }
    }
}

// Consider rename to bind_queue_settings , unbind_queue_settings
void tt_runtime_workload::bind_queue_field_vars(std::vector<tt_queue_setting_info> &queue_settings, string prog_name) {
    vector<tt_queue_setting_info>::iterator it;
    netlist_program &program = get_program(prog_name);
    tt_runtime_queue_ptrs_wrap &qptrs_wrap = get_qptrs_wrap(prog_name);
    for (it = queue_settings.begin(); it != queue_settings.end(); ++it) {
        string queue_name = it->name;
        qptrs_wrap.map_queue_field_var(tt_queue_header_field::GlobalRdptr, queue_name, it->rd_ptr_global);
        qptrs_wrap.map_queue_field_var(tt_queue_header_field::GlobalWrptr, queue_name, it->wr_ptr_global);
        qptrs_wrap.map_queue_field_var(tt_queue_header_field::LocalRdptr,  queue_name, it->rd_ptr_local);
        qptrs_wrap.map_queue_field_var(tt_queue_header_field::ZeroSetting, queue_name, it->zero);
        qptrs_wrap.erase_pending(queue_name); // caller of this function pushes update right away, no more pending
    }
}

void tt_runtime_workload::unbind_queue_field_vars(string queue_name, string prog_name) {
    tt_runtime_queue_ptrs_wrap &qptrs_wrap = get_qptrs_wrap(prog_name);
    for (auto &field_type : qptrs_wrap.supported_fields) {
        qptrs_wrap.map_queue_field_var(field_type, queue_name, "");
    }
    qptrs_wrap.erase_pending(queue_name);
}

void tt_runtime_workload::add_pending_update_queues(string prog_name, string var_name) {
    tt_runtime_queue_ptrs_wrap &qptrs_wrap = get_qptrs_wrap(prog_name);
    log_assert(qptrs_wrap.is_field_type_var(tt_queue_header_field::GlobalRdptr, var_name), "Incorrect type for {}", var_name);
    netlist_program &program = get_program(prog_name);
    int global_ptr = program.get_variable(var_name).value;
    for (std::string q_name : qptrs_wrap.header_field_vars_to_queue_map[tt_queue_header_field::GlobalRdptr][var_name]) {
        // Add queue to pending update list to be pushed to device
        bool is_dual_view_ram = dual_view_rams_to_type_alt_view_map.find(q_name) != dual_view_rams_to_type_alt_view_map.end();
        bool read_only = is_dual_view_ram && dual_view_rams_to_type_alt_view_map.at(q_name).first == DualViewRamType::ReadOnly;
        qptrs_wrap.update_pending(q_name, read_only);
    }
}

void tt_runtime_workload::add_pending_varinst_update_queues(string prog_name, tt_instruction_info instrn, tt_queue_varinst_update_field_mask update_field_mask) {
    log_assert(instrn.opcode == INSTRUCTION_OPCODE::VarInst, "Must be used on Varinst only");
    tt_runtime_queue_ptrs_wrap &qptrs_wrap = get_qptrs_wrap(prog_name);
    qptrs_wrap.pending_varinst_queue_updates.push_back({instrn, update_field_mask});
}

buffer_range_t tt_runtime_workload::get_buffer_range(string queue_name, int buf_id) {
    string buffer_name = queue_name + "_" + std::to_string(buf_id);
    // buffer_ranges_map is constructed when needed, each range consists of a start and end alloc_info
    if (buffer_ranges_map.find(buffer_name) == buffer_ranges_map.end()) {
        tt_queue_info &queue_info = queues[queue_name].my_queue_info;
        uint32_t entry_size = get_entry_size_in_bytes(queue_info, has_tilized_data(queue_name));
        uint32_t buf_size = entry_size * queue_info.entries + tt::io::io_queue_header_size_bytes;

        tt_queue_allocation_info buf = queue_info.alloc_info.at(buf_id);
        buffer_ranges_map.emplace(buffer_name, buffer_range_t{queue_name, queue_info.target_device, queue_info.src_device_id, buf.channel, buf.address, buf.address + buf_size});

    }
    return buffer_ranges_map.at(buffer_name);
}

bool tt_runtime_workload::check_buffer_overlap(buffer_range_t lhs, buffer_range_t rhs, bool host) const {
    //If host is true, checking for overlaps in queues allocated on host.
    // Check for same thing twice (redundancy)
    bool lhs_check = lhs.end_addr > rhs.start_addr && lhs.start_addr < rhs.end_addr;
    bool rhs_check = rhs.end_addr > lhs.start_addr && rhs.start_addr < lhs.end_addr;
    log_assert(lhs_check == rhs_check, "In function check_buffer_overlap: lhs_check and rhs_check must match.");

    // Two buffers can only overlap if they share address values, are on the same DRAM channel and src_device/target_device (belong to the same address space)
    return (lhs_check || rhs_check) && lhs.channel == rhs.channel && (host ? lhs.src_device_id == rhs.src_device_id : lhs.device_id == rhs.device_id);
}

void tt_runtime_workload::check_queues_do_not_overlap() {
    unordered_set<std::string> static_dram_queues = {};
    unordered_set<std::string> static_host_queues = {};
    unordered_set<std::string> dynamic_dram_queues = {};
    generate_queue_to_buf_range_map();
    for(const auto& queue : queues) {
        if(is_static_queue(queue.first)) {
            if (queue.second.my_queue_info.loc == QUEUE_LOCATION::DRAM) {
                static_dram_queues.insert(queue.first);
            }
            else if(queue.second.my_queue_info.loc == QUEUE_LOCATION::HOST) {
                static_host_queues.insert(queue.first);
            }
        }
        else {
            log_assert(queue.second.my_queue_info.loc == QUEUE_LOCATION::DRAM, "Dynamic Host queues are not supported: {}!", queue.first);
            dynamic_dram_queues.insert(queue.first);
        }
    }

    // Check if static DRAM queues overlap
    find_buffer_overlap_across_queues(static_dram_queues, false);
    // Check if static Host queues overlap
    find_buffer_overlap_across_queues(static_host_queues, true);
    // Check if dynamic DRAM queues overlap
    check_dynamic_queue_overlap();
    // Check if static and dynamic DRAM queues overlap
    find_buffer_overlap_across_queues(static_dram_queues, dynamic_dram_queues);
}

void tt_runtime_workload::generate_queue_to_buf_range_map() {
    for(const auto& queue : queues) {
        queues_to_buf_range.insert({queue.first, {}});
        auto& queue_buffers = queue.second.my_queue_info.alloc_info;
        for(int buf_id = 0; buf_id < queue_buffers.size(); buf_id++) {
            queues_to_buf_range.at(queue.first).push_back(get_buffer_range(queue.first, buf_id));
        }
    }
}

void tt_runtime_workload::check_dynamic_queue_overlap() {
    std::unordered_set<std::string> allocated_dynamic_queues = {};
    for(const auto &program : programs) {
        for(const auto &instruction : program.second.get_program_trace()) {
            if(instruction.opcode == INSTRUCTION_OPCODE::AllocateQueue) {
                std::unordered_set<std::string> queues_allocated_at_this_instrn = {};
                for(const auto& queue : instruction.vars) {
                    queues_allocated_at_this_instrn.insert(get<0>(queue));
                }
                find_buffer_overlap_across_queues(queues_allocated_at_this_instrn, false);
                find_buffer_overlap_across_queues(queues_allocated_at_this_instrn, allocated_dynamic_queues);

                allocated_dynamic_queues.merge(queues_allocated_at_this_instrn);
            }
            else if(instruction.opcode == INSTRUCTION_OPCODE::DeallocateQueue) {
                for(const auto& queue : instruction.vars) {
                    allocated_dynamic_queues.erase(get<0>(queue));
                }
            }
        }
    }
}

void tt_runtime_workload::find_buffer_overlap_across_queues(const std::unordered_set<string>& queue_names_left, const std::unordered_set<string>& queue_names_right) {
    std::string fatal_msg = "";
    for(const auto& left_q : queue_names_left) {
        auto& left_bufs = queues_to_buf_range.at(left_q);
        for(const auto& right_q : queue_names_right) {
            auto& right_bufs = queues_to_buf_range.at(right_q);
            for(const auto& left_buf : left_bufs) {
                for(const auto& right_buf : right_bufs) {
                    if(not is_aliased_io(left_q, right_q)) {
                        // Used for dynamic queues. These are always in DRAM
                        if(check_buffer_overlap(left_buf, right_buf, false)) {
                            fatal_msg += fmt::format("The following device buffers overlap: {} and {}", left_buf, right_buf);
                        }
                    }
                }
            }
        }
    }
    log_assert(not fatal_msg.size(), fatal_msg.c_str());
}

void tt_runtime_workload::find_buffer_overlap_across_queues(const std::unordered_set<string>& queue_names, bool host){
    std::vector<std::pair<string, buffer_range_t>> bufs_across_queues = {};
    std::string fatal_msg = "";

    for(const std::string& queue : queue_names){
        for(const auto& buf : queues_to_buf_range.at(queue)) {
            bufs_across_queues.push_back({queue, buf});
        }
    }
    for(auto lhs = 0; lhs != bufs_across_queues.size(); lhs++){
        for(int rhs = lhs + 1; rhs != bufs_across_queues.size(); rhs++){
            if (not is_aliased_io(bufs_across_queues[lhs].first, bufs_across_queues[rhs].first)) {
                if(check_buffer_overlap(bufs_across_queues[lhs].second, bufs_across_queues[rhs].second, host)){
                    if(host){
                        fatal_msg += fmt::format("The following host buffers overlap: {} and {}", bufs_across_queues[lhs].second, bufs_across_queues[rhs].second);
                    }
                    else {
                        fatal_msg += fmt::format("The following device buffers overlap: {} and {}", bufs_across_queues[lhs].second, bufs_across_queues[rhs].second);
                    }
                }
            }
        }
    }
    log_assert(not fatal_msg.size(), fatal_msg.c_str());
}

bool tt_runtime_workload::is_cross_chip_e2e_queue(string queue_name) {
    if (is_e2e_queue(queue_name)) {
        bool same_chip_e2e = true;
        tt_queue_producer_info_t &p_info = queues.at(queue_name).my_producer_info;
        tt_queue_consumer_info_t &c_info = queues.at(queue_name).my_consumer_info;
        int resident_chip_id = queues.at(queue_name).my_queue_info.target_device;

        for (auto &p_graph : p_info.graphs) {
            same_chip_e2e &= (resident_chip_id == graphs.at(p_graph).my_graph_info.target_device);
        }
        for (auto &c_graph : c_info.graphs) {
            same_chip_e2e &= (resident_chip_id == graphs.at(c_graph).my_graph_info.target_device);
        }
        return !same_chip_e2e;
    }
    return false;
}


void tt_runtime_workload::allocate_queue(string queue_name, string prog_name) {
    log_assert(queues[queue_name].my_queue_info.loc == QUEUE_LOCATION::DRAM, "Dyanmic queue is only supported in DRAM!");
    allocated_dynamic_queues.insert(queue_name);
    unbind_queue_field_vars(queue_name, prog_name);  // unbind queue pointers to remove side-effects as queues are re-allocated
}

void tt_runtime_workload::deallocate_queue(string queue_name, string prog_name, bool immediate) {
    unbind_queue_field_vars(queue_name, prog_name); // unbind queue pointers to remove side-effects as queues are deallocated
    if (immediate) {
        allocated_dynamic_queues.erase(queue_name);
        pending_deallocate_dynamic_queues.erase(queue_name);
    } else {
        pending_deallocate_dynamic_queues.insert(queue_name);
    }
}

std::set<string> tt_runtime_workload::collect_overlapped_queues(string queue_name) {
    std::set<string> result;
    auto &target_buffers = queues[queue_name].my_queue_info.alloc_info;
    for (const string allocated_queue : allocated_dynamic_queues) {
        auto &allocated_buffers = queues[allocated_queue].my_queue_info.alloc_info;
        bool has_overlap = false;
        for (size_t allocated_buf_id = 0; allocated_buf_id < allocated_buffers.size(); allocated_buf_id++) {
            buffer_range_t lhs = get_buffer_range(allocated_queue, allocated_buf_id);
            for (size_t target_buf_id = 0; target_buf_id < target_buffers.size(); target_buf_id++) {
                buffer_range_t rhs = get_buffer_range(queue_name, target_buf_id);
                has_overlap = check_buffer_overlap(lhs, rhs);
                if (has_overlap) {
                    goto queue_overlap;
                }
            }
        }
    queue_overlap:
        if (has_overlap) {
            result.insert(allocated_queue);
        }
    }
    return result;
}

tt_dram_io_desc tt_runtime_workload::get_io_desc_for_queue(string queue_name) {
    log_assert (queues.find(queue_name) != queues.end(), "Queue being looked up doesn't exist...");
    tt_queue_info &queue_info = queues[queue_name].my_queue_info;
    tt_dram_io_desc io_desc = get_tt_dram_io_desc_from_tt_queue_info(queue_info);

    log_trace(LogRuntime, "Get IO desc for {}, queue buffer epoch_tiles={}, {}", queue_name, get_entry_size_in_tiles(queue_info), queue_info);

    for (int rr=0; rr<queue_info.grid_size.r; rr++) {
        for (int cc=0; cc<queue_info.grid_size.c; cc++) {
            int buf_id = rr*queue_info.grid_size.c + cc;
            log_assert(buf_id < queue_info.alloc_info.size(), "Buffer must exist in queue alloc_info");
            auto &alloc = queue_info.alloc_info[buf_id];
            io_desc.bufq_start_addr_channel.push_back({alloc.address, alloc.channel});
        }
    }
    return io_desc;
}

tt_runtime_queue_ptrs_wrap &tt_runtime_workload::get_qptrs_wrap(string prog_name) {
    if (program_qptrs_wrap.find(prog_name) == program_qptrs_wrap.end()) {
        program_qptrs_wrap[prog_name] = tt_runtime_queue_ptrs_wrap();
    }
    return program_qptrs_wrap.at(prog_name);
}

std::vector<std::vector<int>> tt_runtime_workload::collect_used_tensix_cores() {
    std::vector<std::vector<int>> cores = {};

    for (std::pair<string, tt_digraph> graph : graphs) {
        const map<string, tt_op_info> &op_map = graph.second.my_graph_info.op_map;
        map<string, tt_op_info>::const_iterator it_op;

        for (it_op = op_map.begin(); it_op != op_map.end(); it_op++) {
            std::shared_ptr<tt_op> op = it_op->second.my_op;
            // Update max grid size needed
            std::pair<int,int> max_core_id = op->get_max_core_id();
            for (const std::vector<tt_core *> &core_row : op->cores) {
                for (tt_core *core : core_row) {
                    cores.push_back({(int)core->get_logical_absolute_row_id(), (int)core->get_logical_absolute_col_id()});
                }
            }
        }
    }
    return cores;
}


std::vector<int> compute_grid_size_accommodating_extra_cores(int num_extra_cores_needed, vector<int> init_grid, buda_soc_description *default_full_soc_desc_ptr){
    int num_passes = 0;
    // In the worst case, the loop below should be done in 2 iterations (one for row expansion and one for col expansion). Ideally a single expansion is sufficient, we need 2 if the dimension getting expanded hits the device dimension bounds.
    while(num_extra_cores_needed > 0){
        // The amount the grid can be expanded in either dimension is constrained by the device size.
        // Max expansion of columns is desired to accommodate all cores but total number of columns cannot exceed grid_size.x
        float expansion_c = std::min(ceil((float)(num_extra_cores_needed)/init_grid[0]), (float)(default_full_soc_desc_ptr->grid_size.x - init_grid[1]));
        float expansion_r = std::min(ceil((float)(num_extra_cores_needed)/init_grid[1]), (float)(default_full_soc_desc_ptr->grid_size.y - init_grid[0]));

        if((init_grid[1] * expansion_r <= init_grid[0] * expansion_c and expansion_r) or !expansion_c){
            // If it is more efficient to expand the rows or if columns cannot be expanded, then expand in the y dim
            init_grid[0] += expansion_r;
            num_extra_cores_needed -= init_grid[1] * expansion_r;
        }

        if((init_grid[0] * expansion_c < init_grid[1] * expansion_r and expansion_c) or !expansion_r){
            // If it is more efficient to expand the columns or if rows cannot be expanded, then expand in the x dim
            init_grid[1] += expansion_c;
            num_extra_cores_needed -= init_grid[0] * expansion_c;
        }
        num_passes++;
        log_assert(num_passes < 2, "Allocating a grid for the SOC Descriptor should not take more than 2 passes. Please check if all cores in the netlist are on the device.");
    }    
    return init_grid;
}

bool not_in_bounding_box(vector<int> core_rc, vector<int> bb_coords){
    //bb_coords are given as {min_r, max_r, min_c, max_c}
    return core_rc[0] < bb_coords[0] or core_rc[0] > bb_coords[1] or core_rc[1] < bb_coords[2] or core_rc[1] > bb_coords[3];
}

std::vector<int> tt_runtime_workload::compute_max_grid(const tt::ARCH &arch, buda_soc_description *default_full_soc_desc_ptr) {
    log_assert(default_full_soc_desc_ptr != NULL, "expected SOC desc to be populated");
    
    uint num_dram_cores_used = 0; //Total number of DRAM cores used by netlist
    uint num_dram_cores_in_grid = 0; //Total number of DRAM cores that are in the original grid sized to contain worker cores
    uint num_dram_cores_to_accommodate = 0; //Number of cores that are not in the original grid
    uint num_cores_in_worker_grid; //Number of cores in a rectangular bounding box around the worker cores
    uint num_empty_cores = 0; //Number of empty cores (not used by workers or DRAM) in the original grid
    uint num_extra_cores_needed; //Number of extra cores needed to accomodate any left over DRAM cores
    
    std::vector<int> max_grid = {0,0};
    auto chan_map = default_full_soc_desc_ptr->dram_core_channel_map;
    const std::vector<std::vector<int>> used_tensix_cores = collect_used_tensix_cores();
    std::map<int, std::vector<int>> used_dram_channel_subchannels = {};

    int min_worker_r = 0x7fffffff; //Max int value
    int min_worker_c = 0x7fffffff;
    int max_worker_r = 0;
    int max_worker_c = 0;

    vector<vector<int>> physical_cores_used = {}; //Physical coords of all cores used in the original grid (just accomodating worker cores)
    vector<vector<int>> dram_cores_in_worker_grid = {};

    for (const std::vector<int> &tensix_core : used_tensix_cores) {
        auto core_r = default_full_soc_desc_ptr->worker_log_to_routing_y.at(tensix_core[0]);
        auto core_c = default_full_soc_desc_ptr->worker_log_to_routing_x.at(tensix_core[1]);
        physical_cores_used.push_back({core_r, core_c});

        min_worker_r = std::min(min_worker_r, core_r);
        min_worker_c = std::min(min_worker_c, core_c);

        max_grid[0] = std::max(max_grid[0], core_r);
        max_grid[1] = std::max(max_grid[1], core_c);
        const auto [dram_channel, subchannel] = tt_epoch_dram_manager::get_dram_chan(arch, tt_xy_pair(core_c, core_r), chan_map);

        if(std::find(used_dram_channel_subchannels[dram_channel].begin(), used_dram_channel_subchannels[dram_channel].end(), subchannel) == used_dram_channel_subchannels[dram_channel].end()){
            num_dram_cores_used += 1; //Increment the number of dram cores used, if a new channel-subchannel combination is found.
            used_dram_channel_subchannels[dram_channel].push_back(subchannel);
            const tt_xy_pair &dram_core_loc = default_full_soc_desc_ptr->dram_cores.at(dram_channel).at(subchannel);
        }
    }
    
    num_cores_in_worker_grid = (max_grid[0] - min_worker_r + 1) * (max_grid[1] - min_worker_c + 1); //Size of the bounding box around all worker cores. Must not put DRAM cores in this box.

    // Account for any valid DRAM cores present inside the worker bounding box. We can move other DRAM cores here.
    for(int c = 0; c < (default_full_soc_desc_ptr -> dram_cores).size(); c++){
        for(int sc = 0; sc < (default_full_soc_desc_ptr -> dram_cores)[c].size(); sc++){
            std::vector<int> core_rc = {(int)(default_full_soc_desc_ptr -> dram_cores)[c][sc].y, (int)(default_full_soc_desc_ptr -> dram_cores)[c][sc].x};
            if(!not_in_bounding_box(core_rc, {min_worker_r, max_worker_r, min_worker_c, max_worker_c})){
                num_cores_in_worker_grid -= 1;
                dram_cores_in_worker_grid.push_back(core_rc);
            }
        }
    }
    max_worker_c = max_grid[1];
    max_worker_r = max_grid[0];

    uint dram_chan;
    for(auto const& queue : this->queues){
        for (int r = 0; r < queue.second.my_queue_info.grid_size.r; r++) {
            for (int c = 0; c < queue.second.my_queue_info.grid_size.c; c++) {

                if (queue.second.my_queue_info.loc == QUEUE_LOCATION::DRAM) {
                    dram_chan = get_queue_dram_channel(queue.second.my_queue_info, r, c);

                    if(std::find(used_dram_channel_subchannels[dram_chan].begin(), used_dram_channel_subchannels[dram_chan].end(), 0) == used_dram_channel_subchannels[dram_chan].end()){
                        num_dram_cores_used += 1;   //Increment the number of dram cores used, if a new channel-subchannel combination is found.
                        used_dram_channel_subchannels[dram_chan].push_back(0); //add subchannel 0 for dram channel from queues
                    }
                }
            }
        }
    }

    // core id's enumerate from 0,0 so need to increment by 1
    max_grid[0] += 1;
    max_grid[1] += 1;
    bool disable_gs_dram_replacement = parse_env("TT_BACKEND_DISABLE_DRAM_REPLACEMENT", false);
    if(arch == tt::ARCH::GRAYSKULL and not disable_gs_dram_replacement){
        //Increment over all used dram cores and account for those already in the grid
        for (const auto &[dram_channel, used_subchannels] : used_dram_channel_subchannels) {
            for (const int subchannel : used_subchannels) {
                const tt_xy_pair &dram_core_loc = default_full_soc_desc_ptr->dram_cores.at(dram_channel).at(subchannel);
                if((int)dram_core_loc.y  < max_grid[0] && (int)dram_core_loc.x < max_grid[1]){
                    physical_cores_used.push_back({(int)dram_core_loc.y, (int)dram_core_loc.x});
                    num_dram_cores_in_grid += 1;
                }
            }
        }
     
        num_dram_cores_to_accommodate = num_dram_cores_used - num_dram_cores_in_grid; //All dram cores not in the grid need to be accomodated
        num_empty_cores = max_grid[0]*max_grid[1] - num_dram_cores_in_grid - num_cores_in_worker_grid; //Empty cores in the grid: total cores in grid - dram cores in grid - worker cores in grid
        num_extra_cores_needed = std::max(0, (int)(num_dram_cores_to_accommodate - num_empty_cores)); //Extra cores needed

        //Compute the max grid size based on the number of extra cores that are needed
        max_grid = compute_grid_size_accommodating_extra_cores(num_extra_cores_needed, max_grid, default_full_soc_desc_ptr);
        
        vector<vector<int>> empty_physical_cores_in_grid = {}; //All physical cores where the unaccomodated DRAM cores can be placed

        // Loop over all cores in grid and identify the ones that are empty
        for(int r = 0; r < max_grid[0]; r++){
            for(int c = 0; c < max_grid[1]; c++){
                std::vector<int> core_rc = {r, c};
                if(std::find(physical_cores_used.begin(), physical_cores_used.end(), core_rc) == physical_cores_used.end() and (not_in_bounding_box(core_rc, {min_worker_r, max_worker_r, min_worker_c, max_worker_c}) or std::find(dram_cores_in_worker_grid.begin(), dram_cores_in_worker_grid.end(), core_rc) != dram_cores_in_worker_grid.end())){
                    // We have an empty core if its not used as a worker or DRAM and is not in the bounding box as an existing worker core. 
                    empty_physical_cores_in_grid.push_back(core_rc);
                }
            }
        }
        // Loop over all DRAM cores. Identify the ones not already in the grid and change their coords to fit the grid.
        for(const auto&[dram_channel, used_subchannels] : used_dram_channel_subchannels){
            for(const int subchannel : used_subchannels){
                const tt_xy_pair &dram_core_loc = default_full_soc_desc_ptr->dram_cores.at(dram_channel).at(subchannel);

                if((int)dram_core_loc.y >= max_grid[0] || (int)dram_core_loc.x >= max_grid[1]){
                    // The core is not in the grid
                    default_full_soc_desc_ptr->dram_cores.at(dram_channel).at(subchannel).x = empty_physical_cores_in_grid[0][1];
                    default_full_soc_desc_ptr->dram_cores.at(dram_channel).at(subchannel).y = empty_physical_cores_in_grid[0][0];
                    // This core is now being used
                    empty_physical_cores_in_grid = std::vector<std::vector<int>>(empty_physical_cores_in_grid.begin() + 1, empty_physical_cores_in_grid.end());
                }
                //The core assigned to the DRAM channel-subchannel can no longer be a worker core
                (default_full_soc_desc_ptr)->workers.erase(std::remove((default_full_soc_desc_ptr->workers).begin(), (default_full_soc_desc_ptr->workers).end(), dram_core_loc), (default_full_soc_desc_ptr->workers).end());
            }
        }
    }
    else{
        for (const auto &[dram_channel, used_subchannels] : used_dram_channel_subchannels) {
            for (const int subchannel : used_subchannels) {
                const tt_xy_pair &dram_core_loc = default_full_soc_desc_ptr->dram_cores.at(dram_channel).at(subchannel);
                max_grid[0] = std::max(max_grid[0], (int)dram_core_loc.y + 1);
                max_grid[1] = std::max(max_grid[1], (int)dram_core_loc.x + 1);
            }
        }
    }
    return max_grid;
}

std::shared_ptr<std::vector<uint32_t>> tt_runtime_workload::allocate_untilized_memory(int num_elements) {
    auto queue_vector = std::make_shared<std::vector<uint32_t>>(num_elements);
    tt::mem::host_shared_memory.insert({reinterpret_cast<void *>(queue_vector->data()), queue_vector});
    allocated_tensor_memories.push_back(reinterpret_cast<void *>(queue_vector->data()));
    return queue_vector;
}
std::shared_ptr<std::vector<uint32_t>> tt_runtime_workload::allocate_untilized_memory(std::string q_name, int num_entries) {
    tt_queue_info &q_info = queues.at(q_name).my_queue_info;

    int vector_size = num_entries * get_tensor_size_in_bytes(q_info, false) / sizeof(uint32_t);
        // Bfp2*, Bfp4* and Bfp8* formats get untilized to Fp16* formats. Allocate Fp16 memory for these tensors.
    if(q_info.data_format == tt::DataFormat::Bfp8 or q_info.data_format == tt::DataFormat::Bfp8_b or
         q_info.data_format == tt::DataFormat::Bfp4 or q_info.data_format == tt::DataFormat::Bfp4_b or
           q_info.data_format == tt::DataFormat::Bfp2 or q_info.data_format == tt::DataFormat::Bfp2_b) {
        auto temp_queue = q_info;
        temp_queue.data_format = tt::DataFormat::Float16;
        vector_size = num_entries * get_tensor_size_in_bytes(temp_queue, false) / sizeof(uint32_t);
    }

    auto queue_vector = std::make_shared<std::vector<uint32_t>>(vector_size);
    tt::mem::host_shared_memory.insert({reinterpret_cast<void *>(queue_vector->data()), queue_vector});
    allocated_tensor_memories.push_back(reinterpret_cast<void *>(queue_vector->data()));
    return queue_vector;
}

std::shared_ptr<std::vector<uint32_t>> tt_runtime_workload::allocate_tilized_memory(const std::string& q_name, uint32_t num_entries) {
    tt_queue_info &q_info = queues.at(q_name).my_queue_info;
    std::uint32_t size_in_bytes = get_tensor_size_in_bytes(q_info, q_info.layout == IO_LAYOUT::Tilized);
    std::uint32_t vector_size = num_entries * size_in_bytes / sizeof(uint32_t);
    auto tilized_vector = std::make_shared<std::vector<uint32_t>>(vector_size);
    tt::mem::host_shared_memory.insert({reinterpret_cast<void *>(tilized_vector->data()), tilized_vector});
    allocated_tensor_memories.push_back(reinterpret_cast<void *>(tilized_vector->data()));
    return tilized_vector;
}

void tt_runtime_workload::deallocate_memory(void* ptr) {
    if (tt::mem::host_shared_memory.find(ptr) != tt::mem::host_shared_memory.end()) {
        log_trace(tt::LogRuntime, "Deallocating {} -- shared_ptr use_count={}",
            reinterpret_cast<intptr_t>(ptr),
            tt::mem::host_shared_memory.at(ptr).use_count());
        log_assert(tt::mem::host_shared_memory.at(ptr).use_count() == 1, "Use count for shared_ptr must be 0 before deallocation.");
        tt::mem::host_shared_memory.erase(ptr);
    } else {
        log_trace(LogRuntime, "Skip deallocating {}, either it's already freed or was never allocated", reinterpret_cast<intptr_t>(ptr));
    }
}
/* Only return queue settings when called for the first execute of the temporal epoch */
vector<tt_queue_setting_info> tt_runtime_workload::collect_temporal_epoch_instance_queue_settings(netlist_program &program, int target_device) {

    auto queue_settings = vector<tt_queue_setting_info>();
    auto execute_statements = this->get_execute_statements_belonging_to_current_temporal_graph_instance(program);
    std::unordered_set<std::string> visited_queue_names = {};

    for (const auto execute_statement_index : execute_statements) {
        const auto &exec_instrn = program.get_program_trace().at(execute_statement_index);
        for (const auto &queue_setting : exec_instrn.queue_settings) {
            auto &queue_info = queues.at(queue_setting.name).my_queue_info;
            // Filter queue settings that are targeting queues on current execute instruction's device.
            if (queue_info.target_device == target_device){
                if (visited_queue_names.find(queue_setting.name) == visited_queue_names.end()) {
                    queue_settings.push_back(queue_setting);
                    visited_queue_names.insert(queue_setting.name);
                }
            }
        }
    }

    return queue_settings;
}

ostream& operator<<(ostream& out, const buffer_range_t& address_range){
    out << "(queue_name: " << address_range.queue_name << ", start_addr : 0x" << std::hex << address_range.start_addr 
        << ", end_addr: 0x" << std::hex << address_range.end_addr << ", device: " << address_range.device_id << ", channel: " 
        << address_range.channel << ")";
    return out;
}
set<string> tt_runtime_workload::collect_output_e2e_queues(string graph_name) {
    static unordered_map<string, set<string>> graph_to_out_e2e_qs;
    set<string> out_e2e_qs;

    if (graph_to_out_e2e_qs.count(graph_name)) {
        out_e2e_qs = graph_to_out_e2e_qs.at(graph_name);
    } else {
        for (const string &e2e_queue : e2e_queues) {
            if (queues.at(e2e_queue).my_producer_info.graphs.find(graph_name) !=
                queues.at(e2e_queue).my_producer_info.graphs.end()) {
                out_e2e_qs.insert(e2e_queue);
            }
        }
    }
    return out_e2e_qs;
}

set<int> tt_runtime_workload::collect_output_e2e_devices(string graph_name) {
    set<int> out_e2e_devices;
    set<string> out_e2e_qs = collect_output_e2e_queues(graph_name);
    for (const string &e2e_queue : out_e2e_qs) {
        log_assert(queues.at(e2e_queue).my_queue_info.loc == QUEUE_LOCATION::DRAM, "Expected e2e queue in DRAM");
        out_e2e_devices.insert(queues.at(e2e_queue).my_queue_info.target_device);
    }
    return out_e2e_devices;
}

set<string> tt_runtime_workload::collect_pending_dealloc_queues(set<int> &target_devices) {
    set<string> dealloc_qs;
    for (const string &q : pending_deallocate_dynamic_queues) {
        if (target_devices.find(queues.at(q).my_queue_info.target_device) != target_devices.end()) {
            dealloc_qs.insert(q);
        }
    }
    return dealloc_qs;
}

void tt_runtime_workload::print_cross_chip_e2e_queues() {
    // Store total bytes for each src and dst device pair
    std::map<std::pair<int, int>, pair<int, int>> cross_chip_total_bytes;
    for (const auto &e2e_queue : e2e_queues) {
        const auto &q_info = queues.at(e2e_queue).my_queue_info;
        const auto &p_info = queues.at(q_info.name).my_producer_info;
        const bool include_tile_header_size = q_info.layout != IO_LAYOUT::Flat;
        log_assert(p_info.graphs.size() == 1, "E2E queue should only have one producer graph");
        if (is_cross_chip_e2e_queue(q_info.name)) {
            int src_device = -1;
            int dst_device = q_info.target_device;
            for (auto &p_graph : p_info.graphs) {
                src_device = graphs.at(p_graph).my_graph_info.target_device;
            }
            int entry_size = tt::size::get_entry_size_in_bytes(q_info.data_format, include_tile_header_size, q_info.dim.ublock_ct, q_info.dim.ublock_rt, q_info.dim.mblock_m, q_info.dim.mblock_n, q_info.dim.t, get_tile_dim_y(q_info), get_tile_dim_x(q_info));
            int buf_size = entry_size * q_info.entries + tt::io::io_queue_header_size_bytes;
            int num_bufs = q_info.grid_size[0] * q_info.grid_size[1];
            int io_size = buf_size * num_bufs;
            log_trace(LogRuntime, "Cross chip {}->{} e2e queue: {:<40}, entry per core = {:>4} KB, microbatch = {}, per core = {:>6} KB, total = {:.2f} MB", 
                src_device, dst_device, q_info.name, entry_size/1024, q_info.input_count, buf_size/1024, (float)io_size/1024/1024);
            if (cross_chip_total_bytes.find({src_device, dst_device}) == cross_chip_total_bytes.end()) {
                cross_chip_total_bytes[{src_device, dst_device}] = {0, 0};
            }
            cross_chip_total_bytes[{src_device, dst_device}].first  += entry_size*num_bufs;
            cross_chip_total_bytes[{src_device, dst_device}].second += io_size;
        }
    }
    for (const auto &kv : cross_chip_total_bytes) {
        log_trace(LogRuntime, "Cross chip e2e queue total bytes: src_device = {}, dst_device = {}, io per input = {:.2f} MB, total_bytes = {:.2f} MB", kv.first.first, kv.first.second, (float)kv.second.first/(1024*1024), (float)kv.second.second/(1024*1024));
    }
}


std::string tt_runtime_workload::get_base_queue_name(const tt_queue_info &queue_info) {
    return queue_info.alias != "" ? queue_info.alias : queue_info.name;
}


// Collect the set of dual view rams (both base name and aliased name) and their type (read/write only)
void tt_runtime_workload::identify_all_dual_view_rams() {
    for (auto &queue : queues) {
        const tt_queue_info &queue_info = queue.second.my_queue_info;
        if (queue_info.type == tt::IO_TYPE::RandomAccess) {
            std::string queue_name = queue.first;
            std::string base_name = get_base_queue_name(queue_info);
            if (base_name != queue_name) {
                dual_view_rams_to_info_map[queue_name] = {DualViewRamType::Unknown, {base_name}}; // {type, alt_views}
                dual_view_rams_to_info_map[base_name]  = {DualViewRamType::Unknown, {queue_name}}; // {type, alt_views}
            }
        }
    }

    // 2nd pass to determine whether each dual view RAM is read-only or write-only.
    // Kind of a workaround and partial hack since read_only is queue-settings attribute. Would like to make them
    // explicit queue attributes, so this can continue to be identified at initialization time.
    for (const auto &program_name : program_order) {
        for (const auto &instrn : get_program(program_name).get_program_trace()) {
            if (instrn.opcode != INSTRUCTION_OPCODE::Execute) {
                continue;
            }

            std::unordered_set<std::string> dual_view_rams_for_graph;
            for (const auto &queue_setting : instrn.queue_settings) {
                const auto &q_name = queue_setting.name;
                if (dual_view_rams_to_info_map.find(q_name) == dual_view_rams_to_info_map.end()) {
                    continue;
                }

                // Restriction in order for this to be able to run at compile-time. Cannot have variables used by this field
                // which are only known at runtime. Can remove all of this when read_only setting is moved to queue instead.
                // Currently pybuda only ever sets "true" so this should be fine for now.
                if (queue_setting.read_only == "true") {
                    // log_info(tt::LogRuntime, "{} - Setting {} to ReadOnly", __FUNCTION__, q_name);
                    if (dual_view_rams_to_info_map.at(q_name).type == DualViewRamType::Unknown) {
                        dual_view_rams_to_info_map.at(q_name).type = DualViewRamType::ReadOnly;
                    } else {
                        log_assert(dual_view_rams_to_info_map.at(q_name).type == DualViewRamType::ReadOnly,
                            "Queue {} toggled queue-settings read_only attribute, not supported!", q_name);
                    }
                } else if (queue_setting.read_only == "false") {
                    // log_info(tt::LogRuntime, "{} - Setting {} to WriteOnly", __FUNCTION__, q_name);
                    if (dual_view_rams_to_info_map.at(q_name).type == DualViewRamType::Unknown) {
                        dual_view_rams_to_info_map.at(q_name).type = DualViewRamType::WriteOnly;
                    } else {
                        log_assert(dual_view_rams_to_info_map.at(q_name).type == DualViewRamType::WriteOnly,
                            "Queue {} toggled queue-settings read_only attribute, not supported!", q_name);
                    }
                } else {
                    log_fatal("Unsupported value for {} queue_settings.read_only: {} - must be 'true' or 'false' currently.", q_name, queue_setting.read_only);
                }

                dual_view_rams_for_graph.insert(q_name);
            }

            // Determine if graph uses both views (Reader/Writer) of Dual View Rams, so runtime can do rd/wr access overlap check.
            for (auto &q_name : dual_view_rams_for_graph) {
                if (dual_view_rams_to_info_map.at(q_name).type == DualViewRamType::ReadOnly) {
                    for (auto &alt_view_name : dual_view_rams_to_info_map.at(q_name).alt_views) {
                        if (dual_view_rams_for_graph.find(alt_view_name) != dual_view_rams_for_graph.end()) {
                            if (single_graph_to_dual_view_ram_reader_writer_map.find(instrn.graph_name) == single_graph_to_dual_view_ram_reader_writer_map.end()) {
                                // Support only a single pair for now. Unclear how checking for multiple pairs would be handled. This checker may be abandoned anyways.
                                single_graph_to_dual_view_ram_reader_writer_map[instrn.graph_name] = {q_name, alt_view_name};
                            } else {
                                // Disable warning since hazard checking is disabled
                                // log_warning(tt::LogRuntime, "Graph {} has more than 2 dual-view RAM views used, which isn't supported for hazard checking purposes. Ignoring {}", instrn.graph_name, alt_view_name);
                            }
                        }
                    }
                }
            }
        }
    }
}


const std::unordered_map<std::string, dual_view_ram_info_t> &tt_runtime_workload::get_dual_view_rams_map() const {
    return dual_view_rams_to_info_map;
}

const std::pair<std::string, std::string> &tt_runtime_workload::get_single_graph_dual_view_ram_reader_writer(std::string graph_name) const {
    static const std::pair<std::string, std::string> empty_pair;
    if (single_graph_to_dual_view_ram_reader_writer_map.find(graph_name) != single_graph_to_dual_view_ram_reader_writer_map.end()) {
        return single_graph_to_dual_view_ram_reader_writer_map.at(graph_name);
    } else {
        return empty_pair;
    }
}

std::string tt_runtime_workload::get_op_name(const string& graph_name, const tt_xy_pair& logical_core_xy) {
    unsigned int logical_x = logical_core_xy.x;
    unsigned int logical_y = logical_core_xy.y;
    const std::map<string, tt_digraph>& graph_map = this->graphs;
    const auto graph_map_iterator = graph_map.find(graph_name);

    if (graph_map_iterator == graph_map.end()) {
        log_fatal("Cannot find a graph with name {} to return the op name for logical coordinates x = {} and y = {}. "
                  "This message indicates bug in compilation process", graph_name, 
                  std::to_string(logical_x), std::to_string(logical_y));
    }

    const std::map<string, tt_op_info>& op_map = graph_map_iterator->second.my_graph_info.op_map;

    for (const auto& [op_name, op_info] : op_map) {
        if (logical_x >= op_info.grid_loc_x() && logical_x <= op_info.grid_end_x() &&
            logical_y >= op_info.grid_loc_y() && logical_y <= op_info.grid_end_y()) {
            return op_name;
        }
    }

    log_fatal("No op for graph {} with specified logical coordinates x = {} and y = {}. "
              "This message indicates bug in compilation process",
              graph_name, std::to_string(logical_x), std::to_string(logical_y));
}
