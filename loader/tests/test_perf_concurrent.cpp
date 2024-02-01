// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"

#include "tt_rnd_util.hpp"
#include "perf_lib/postprocess.hpp"
#include "common/model/tt_core.hpp"

using namespace verif::random;

perf::tt_backend_perf temp_profiler;

uint get_dram_channel_for_core(tt_xy_pair core, const buda_SocDescriptor *sdesc) {
    for (auto channel_it: sdesc->get_perf_dram_bank_to_workers()) {
        if (std::find(channel_it.second.begin(), channel_it.second.end(), core) != channel_it.second.end()) {
            return std::get<0>(sdesc->dram_core_channel_map.at(channel_it.first));
        }
    }
    log_assert(false, "dram channel not found for core {}",  core.str());
    return 0;
}


// Converts "- 0x..." to an unsigned integer.
uint convert_string_line_to_hex(string input_number_line_str) {
    char bullet;
    uint event_val;
    std::istringstream iss(input_number_line_str);
    iss >> bullet >> std::hex >> event_val;
    return event_val;
}

struct thread_emule {
    tt_xy_pair coord;
    uint chip_id;
    uint thread_id;
    uint num_epochs;
    uint num_dumps_per_epoch = 1;
    uint dump_id = 0;
    uint single_thread_dump_size;

    thread_emule(tt_xy_pair coord, uint chip_id, uint thread_id, uint num_epochs, uint single_thread_dump_size):
                coord(coord), chip_id(chip_id), thread_id(thread_id), num_epochs(num_epochs), single_thread_dump_size(single_thread_dump_size) {}

    bool is_thread_finished() {
        return dump_id == num_dumps_per_epoch * num_epochs;
    }

    perf::PerfDumpHeader get_header() {
        perf::PerfDumpHeader header;
        header.x = coord.x;
        header.y = coord.y;
        header.chip_id = chip_id;
        header.thread_id = thread_id;
        header.epoch_id = dump_id / num_dumps_per_epoch;
        cout << "KD-dump_id = " << dump_id << " num_dumps_per_epoch = " << num_dumps_per_epoch << endl;
        return header;
    }

    void generate_single_thread_dump(shared_ptr<uint32_t> data_ptr) {

        log_assert(dump_id < num_epochs * num_dumps_per_epoch, "Each thread can only dump num_epochs {} * num_dumps_per_epoch {} times into dram.", num_epochs, num_dumps_per_epoch);
        perf::PerfDumpHeader header = get_header();
        dump_id++;
        uint32_t header_val = *(reinterpret_cast<uint32_t*>(&header));
        stringstream ss;
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << header_val << endl;
        log_debug(tt::LogPerfInfra, "Test: Generating thread dump for thread idx {}, with header {}", thread_id, ss.str());

        string file_name = "loader/tests/perf_test_data/thread" + to_string(thread_id) + "_0.yaml";
        log_assert(fs::exists(file_name), "Perf sample dump file does not exist under {}", file_name);
        std::ifstream ifs(file_name);
        string line;
        // vector<uint32_t> perf_events;
        uint idx = 0;
        data_ptr.get()[idx] = perf::valid_thread_dump_start_id;
        idx++;
        data_ptr.get()[idx] = *(reinterpret_cast<uint32_t*>(&header));
        idx++;
        while (std::getline(ifs, line)) {
            // cout << "line = " << line << endl;
            // cout << "val = " << convert_string_line_to_hex(line) << endl;
            data_ptr.get()[idx] = convert_string_line_to_hex(line);
            idx++;
            log_assert(idx < single_thread_dump_size / sizeof(uint32_t),
                "maximum number of events in the samples files must be less than a single perf dump {}. num lines in file read so far = {}", single_thread_dump_size / sizeof(uint32_t), idx);
        }
        log_debug(tt::LogPerfInfra, "Test: Total number of lines read from perf sample file {} = {}", file_name, idx-2);
        while (idx < single_thread_dump_size / sizeof(uint32_t) - 1) {
            data_ptr.get()[idx] = 0;
            idx++;
        }
        log_debug(tt::LogPerfInfra, "Test: Final thread dump: {}", perf::get_thread_dump_partial_ss(data_ptr.get(), single_thread_dump_size / sizeof(uint32_t)).str());
        data_ptr.get()[idx] = perf::thread_dump_end_id;
    }
};

struct core_emule {
    const tt_xy_pair coord;
    uint channel_id;
    uint chip_id;
    vector<thread_emule> all_threads;
    uint num_epochs;
    uint single_thread_dump_size;

    core_emule(tt_xy_pair coord, const buda_SocDescriptor *sdesc, uint chip_id, uint num_epochs, uint single_thread_dump_size):
                coord(coord), chip_id(chip_id), num_epochs(num_epochs), single_thread_dump_size(single_thread_dump_size) {
        for (uint i = 0; i < 4; i++) {
            all_threads.push_back(thread_emule(coord, chip_id, i, num_epochs, single_thread_dump_size));
        }
        channel_id = get_dram_channel_for_core(coord, sdesc);
    }

    bool is_core_finished() {
        for (thread_emule thread: all_threads) {
            if (!thread.is_thread_finished()) {
                return false;
            }
        }
        return true;
    }

    uint generate_single_core_dump(shared_ptr<uint32_t> data_ptr) {
        uint wr_ptr = 0;
        uint num_dumps = 0;
        for (thread_emule &thread: all_threads) {
            log_info(tt::LogPerfInfra, "Starting thread perf dump for chip_id {} dram_channel_id {}, core {}, thread_id {}", chip_id, channel_id, coord.str(), thread.thread_id);
            if (!thread.is_thread_finished()) {
                shared_ptr<uint32_t> new_ptr(new uint32_t[single_thread_dump_size / sizeof(uint32_t)], std::default_delete<uint32_t[]>());
                log_debug(tt::LogPerfInfra, "Generating thread dump for core {} thread id {}", coord.str(), thread.thread_id);
                thread.generate_single_thread_dump(new_ptr);
                std::memcpy(data_ptr.get() + wr_ptr, new_ptr.get(), single_thread_dump_size);
                log_debug(tt::LogPerfInfra, "Finished generating thread dump for core {} thread id {}", coord.str(), thread.thread_id);
                wr_ptr += single_thread_dump_size / sizeof(uint32_t);
                num_dumps++;
            } else {
                log_warning(tt::LogPerfInfra, "Skipping thread dump for chip_id {} dram_channel_id {}, core {}, thread_id {}, since thread is finished", chip_id, channel_id, coord.str(), thread.thread_id);
            }
        }
        return num_dumps;
    }
};

struct dram_channel_emule {
    uint chip_id;
    uint mmio_chip_id = 0;
    uint channel_id;
    postprocess::PerfHostScratchBuffer* host_scratch_buf_ptr;
    uint host_scratch_queue_id;
    std::unordered_map<tt_xy_pair, core_emule> all_cores;
    uint host_dump_slot_size;

    dram_channel_emule(uint chip_id, uint channel_id, uint queue_num_slots, uint host_scratch_queue_id, postprocess::PerfHostScratchBuffer* host_scratch_buf_ptr):
        chip_id(chip_id), channel_id(channel_id),
        host_scratch_queue_id(host_scratch_queue_id), host_scratch_buf_ptr(host_scratch_buf_ptr) {

        host_dump_slot_size = host_scratch_buf_ptr->slot_size_bytes;
    }
    
    bool all_cores_finished() {
        for (auto core_it: all_cores) {
            if (!core_it.second.is_core_finished()) {
                return false;
            }
        }
        return true;
    }

    void generate_single_channel_dump(shared_ptr<uint32_t> data_ptr) {
        if (all_cores_finished()) {
            log_info(tt::LogPerfInfra, "Test: all cores for dram channel id {} chip {} have finished", channel_id, chip_id);
            return;
        }
        uint wr_ptr = 0;
        for (auto &core_it: all_cores) {
            log_info(tt::LogPerfInfra, "Starting core perf dump for chip_id {} dram_channel_id {}, core {}", chip_id, channel_id, core_it.first.str());
            if (!core_it.second.is_core_finished()) {
                shared_ptr<uint32_t> new_ptr(new uint32_t[host_scratch_buf_ptr->single_thread_dump_size * 4 / sizeof(uint32_t)], std::default_delete<uint32_t[]>());
                log_debug(tt::LogPerfInfra, "Generating perf dump for core {}", core_it.first.str());
                uint num_dumps = core_it.second.generate_single_core_dump(new_ptr);
                std::memcpy(data_ptr.get() + wr_ptr, new_ptr.get(), host_scratch_buf_ptr->single_thread_dump_size * num_dumps);
                log_debug(tt::LogPerfInfra, "Finished generating perf dump for core {}", core_it.first.str());
                wr_ptr += host_scratch_buf_ptr->single_thread_dump_size * num_dumps / sizeof(uint32_t);
            } else {
                log_warning(tt::LogPerfInfra, "Skipping perf dump for core {} since core is finished", core_it.first.str());
            }
        }
    }
    
    void push_to_host() {
        if (all_cores_finished()) {
            log_info(tt::LogPerfInfra, "Test: all cores for dram channel id {} chip {} have finished", channel_id, chip_id);
            return;
        }
        if (host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.full()) {
            log_warning(tt::LogPerfInfra, "Test: waiting for space in the device dram dump");
            while (host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.full()) {}
            log_warning(tt::LogPerfInfra, "Test: Finished waiting for space in the device dram dump");
        }

        shared_ptr<uint32_t> new_ptr(new uint32_t[host_dump_slot_size / sizeof(uint32_t)], std::default_delete<uint32_t[]>());
        generate_single_channel_dump(new_ptr);
        
        uint32_t* wr_addr_slot_ptr = reinterpret_cast<uint32_t*>(host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).base_ptr) +
                                host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.get_wr_ptr() * host_dump_slot_size / sizeof(uint32_t);
        
        log_debug(tt::LogPerfInfra, "Test: Writing a new device perf dump to host mmio region chip_id {} channel_idx {} host_queue_idx {} at rd_ptr: {}, wr_ptr: {}, addr: {}",
                                chip_id, channel_id, host_scratch_queue_id, host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.get_rd_ptr(),
                                host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.get_wr_ptr(), (void*)wr_addr_slot_ptr);
        
        std::memcpy(wr_addr_slot_ptr, new_ptr.get(), host_dump_slot_size);
        log_debug(tt::LogPerfInfra, "Test: Finished writing a new device perf dump to host mmio region chip_id {} channel_idx {} host_queue_idx {} at rd_ptr: {}, wr_ptr: {}, addr: {}",
                                chip_id, channel_id, host_scratch_queue_id, host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.get_rd_ptr(),
                                host_scratch_buf_ptr->get_queue_for_mmio_at_index(mmio_chip_id, host_scratch_queue_id).ptr.get_wr_ptr(), (void*)wr_addr_slot_ptr);

    }

    void print() {
        log_info(tt::LogPerfInfra, "dram channel {} chip {}:", channel_id, chip_id);
        for (auto core_it: all_cores) {
            log_info(tt::LogPerfInfra, "core {}, total num epochs {}", core_it.first.str(), core_it.second.num_epochs);
        }
    }
};

struct chip_emule {
    uint chip_id;
    
    vector<dram_channel_emule> dram_cores;
    const buda_SocDescriptor *sdesc;
    const uint base_host_scratch_queue_idx;
    postprocess::PerfHostScratchBuffer* host_scratch_buf_ptr;

    chip_emule(
        uint chip_id, const buda_SocDescriptor *sdesc, postprocess::PerfHostScratchBuffer* host_scratch_buf_ptr,
        const uint base_host_scratch_queue_idx, unordered_map<tt_cxy_pair, uint> core_to_num_epochs):
        chip_id(chip_id), sdesc(sdesc), host_scratch_buf_ptr(host_scratch_buf_ptr), base_host_scratch_queue_idx(base_host_scratch_queue_idx) {
        
        for (uint i = 0; i < sdesc->dram_cores.size(); i++) {
            uint queue_idx = base_host_scratch_queue_idx + i;
            dram_channel_emule dram(chip_id, i, host_mem::address_map::NUM_THREADS_IN_EACH_DEVICE_DUMP, queue_idx, host_scratch_buf_ptr);
            // dram_cores.push_back(dram_channel_emule(chip_id, i, perf::num_device_dump_slots, queue_idx, host_scratch_buf_ptr));
            for (tt_xy_pair worker: sdesc->get_perf_dram_bank_to_workers().at(sdesc->dram_cores.at(i).at(0))) {
                tt_cxy_pair core(chip_id, worker);
                if (core_to_num_epochs.find(core) != core_to_num_epochs.end()) {
                    log_assert(dram.all_cores.find(worker) == dram.all_cores.end(),"");
                    dram.all_cores.insert({worker, core_emule(worker, sdesc, chip_id, core_to_num_epochs.at(core), host_scratch_buf_ptr->single_thread_dump_size)});
                    log_assert(dram.all_cores.size() <= 8, "currently only up to 8 cores per channel is supported for this test");

                }
            }
            dram_cores.push_back(dram);
        }
        log_info(tt::LogPerfInfra, "Test: Created chip id {} with following dram cores and active cores for each dram:", chip_id);
        for (auto dram: dram_cores) {
            dram.print();
        }
    }

    uint get_num_channels() {
        return sdesc->dram_cores.size();
    }

    void generate_single_chip_dump() {
        for (dram_channel_emule &dram: dram_cores) {
            log_info(tt::LogPerfInfra, "Starting dram channel perf dump for chip_id {} dram_channel_id {}", dram.chip_id, dram.channel_id);
            if (!dram.all_cores_finished()) {
                dram.push_to_host();
            } else {
                log_warning(tt::LogPerfInfra,
                    "Test: Skipping perf dump generation for channel_id {} chip_id {} because all cores finished.", dram.channel_id, chip_id);
            }
        }
    }
    
};

uint get_pc_for_graph(const netlist_program &program, const string &graph_name) {
    uint pc = 0;
    for (const tt_instruction_info &instr: program.get_program_trace()) {
        if (instr.opcode == INSTRUCTION_OPCODE::Execute && instr.graph_name == graph_name) {
            return pc;
        }
        pc++;
    }
    log_assert(false,"Instruction for graph name {} not found in program", graph_name);
    return 0;
}

unordered_map<tt_cxy_pair, uint> get_core_to_num_epochs(const map<string, tt_digraph> &graphs, uint num_loops, const buda_SocDescriptor *sdesc) {

    unordered_map<tt_cxy_pair, uint> core_to_num_epochs;
    for (auto graph_it: graphs) {
        for (auto &op_it: graph_it.second.my_graph_info.op_map) {
            string op_name = op_it.first;
            std::shared_ptr<tt_op> op = op_it.second.my_op;
            int grid_shape_y = op->grid_shape.at(0);
            int grid_shape_x = op->grid_shape.at(1);
            for (int x = 0; x < grid_shape_x; x++) {
                for (int y = 0; y < grid_shape_y; y++) {
                    int core_y_id = op->cores.at(y).at(x)->get_logical_absolute_row_id();
                    int core_x_id = op->cores.at(y).at(x)->get_logical_absolute_col_id();

                    int physical_core_y = sdesc->worker_log_to_routing_y.at(core_y_id);
                    int physical_core_x = sdesc->worker_log_to_routing_x.at(core_x_id);
                    tt_cxy_pair core_coord(graph_it.second.my_graph_info.target_device, physical_core_x, physical_core_y);

                    if (core_to_num_epochs.find(core_coord) == core_to_num_epochs.end()) {
                        core_to_num_epochs.insert({core_coord, 0});
                    }
                    core_to_num_epochs.at(core_coord) += num_loops;
                }
            }
        }
    }
    return core_to_num_epochs;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    // Runtime setup
    tt_runtime_config config(args);

    int seed = -1;
    std::tie(seed, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", -1);
    
    verif_args::validate_remaining_args(args);
    string netlist_path = "loader/tests/net_basic/netlist_binary.yaml";
    config.netlist_path = netlist_path;
    config.perf_desc.device_perf_mode = perf::PerfDumpMode::Concurrent;

    if (seed == -1) {
        seed = uint(std::time(NULL));
    }
    log_info(tt::LogTest, "Generating random tensor with seed {}", seed);
    tt_rnd_set_seed(seed);
    tt_runtime runtime(config.netlist_path, config);
    // runtime.config.mode = DEVICE_MODE::CompileOnly;
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

    unordered_set<uint> all_device_ids;

    // uint num_devices = 1;
    uint32_t postprocess_queue_num_slots = 256;
    uint64_t total_time_to_run_host_scratch_buf_us = 1000000;
    int num_loops = 10;

    // if (fs::exists(config.output_dir + "perf_results/")) {
    //     fs::remove_all(config.output_dir + "perf_results/");
    // }

    const tt_runtime_workload *workload = runtime.get_workload();
    for (const auto &graph_it: workload->graphs) {
        all_device_ids.insert(graph_it.second.my_graph_info.target_device);
    }

    // auto sdesc =  std::shared_ptr<tt_soc_description>(load_soc_descriptor_from_yaml(runtime.get_soc_desc_path(0)));
    const std::unordered_map<chip_id_t, buda_soc_description> chip_to_sdesc = runtime.cluster->get_sdesc_for_all_devices();
    
    unordered_map<tt_cxy_pair, uint> core_to_num_epochs = get_core_to_num_epochs(workload->graphs, num_loops, &chip_to_sdesc.at(0));
    log_info(tt::LogPerfInfra, "Setting up chip emulator");
    vector<chip_emule> all_chips;
    for (auto device: all_device_ids) {
        uint base_host_scratch_queue_idx = device * chip_to_sdesc.at(0).dram_cores.size();
        all_chips.push_back(
            chip_emule(device, &chip_to_sdesc.at(0), runtime.perf_host_scratch_buffer.get(), base_host_scratch_queue_idx, core_to_num_epochs));
    }
    log_info(tt::LogPerfInfra, "Finished setting up chip emulator");

    log_assert(workload->program_order.size() == 1, "In this test only netlists with single program are supported.");
    string program_name = workload->program_order.at(0);
    const netlist_program &program = workload->programs.at(program_name);

    for (uint loop = 0; loop < num_loops; loop++) {
        log_info(tt::LogPerfInfra, "KD-Initializing proram");
        const std::unordered_map<chip_id_t, buda_soc_description> chip_to_sdesc = runtime.cluster->get_sdesc_for_all_devices();
        const std::shared_ptr<tt_device> device = runtime.cluster->get_device();
        runtime.cluster->perf_state.initialize_for_program(program, chip_to_sdesc, workload->graphs, workload->op_to_outputs, workload->queues);
        for (const auto &graph_it: workload->graphs) {
            uint chip_id = graph_it.second.my_graph_info.target_device;
            runtime.cluster->perf_state.update_executed_instr(program_name, get_pc_for_graph(program, graph_it.first));
        }
    }

    // log_info(tt::LogPerfInfra, "Starting device query");
    // runtime.perf_host_scratch_buffer->start_query();

    std::thread perf_generator_thread = std::thread([&] {
        try{

            uint64_t start_timestamp = temp_profiler.get_current_timestamp();
            uint64_t total_time_active_ns = 800000 * 1000;
            uint64_t interval_ns = 1 * 1000;
            uint64_t last_timestamp = start_timestamp;
            uint chip_id = 0;
            for (chip_emule &chip: all_chips) {
                for (uint loop_idx = 0; loop_idx < num_loops; loop_idx++) {
                    log_info(tt::LogPerfInfra, "Starting chip perf dump for chip id {}, loop_idx {}", chip_id, loop_idx);
                    chip.generate_single_chip_dump();
                }
                chip_id++;
            }

        } catch (std::exception &e) {
            std::rethrow_exception(std::current_exception());
        }
    });

    perf_generator_thread.join();
    runtime.perf_host_scratch_buffer->stop_device_trace_poll();

    return 0;
}

