// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <sstream>
#include "memory_profiler.hpp"
#include "perf_lib/perf_utils.hpp"

using json = nlohmann::json;
using std::stringstream;

namespace perf {

string to_hex(uint64_t num, uint8_t num_digits) {
    stringstream ss;
    ss << "0x" << std::hex << std::setw(num_digits) << std::setfill('0') << num;
    return ss.str();
}

string get_core_str(const tt_xy_pair &core) {
    return to_string(core.x) + "-" + to_string(core.y);
}

/*********************
 * L1 Buffer Methods  
**********************/

void L1Buffer::update_consumed_size(int new_consumed_size) {
    if (has_reserved_size()) {
        log_assert(new_consumed_size <= m_reserved_size, "{}: Consumed size of buffer {} exceeds reserved size ({} > {})", __FUNCTION__, m_name, new_consumed_size, m_reserved_size);
    }
    m_consumed_size = new_consumed_size;
}

void L1Buffer::update_reserved_size(int new_reserved_size) {
    if (new_reserved_size != buffer_size_undefined) {
        log_assert(m_consumed_size <= new_reserved_size, "{}: Consumed size of buffer {} exceeds reserved size ({} > {})", __FUNCTION__, m_name, m_consumed_size, new_reserved_size);
    }
    m_reserved_size = new_reserved_size;
}


/*********************
 * L1 Core Methods  
**********************/

L1Core::L1Core(const tt_cxy_pair &physical_coord, const tt_cxy_pair &logical_coord, const string &op_name, const string &op_type, uint32_t l1_size)
    : m_device_id(physical_coord.chip), m_physical_coord(physical_coord), m_logical_coord(logical_coord), m_op_name(op_name), m_op_type(op_type), m_l1_size(l1_size) {
    log_assert(physical_coord.chip == logical_coord.chip, "L1Core: physical, logical device ids don't match");
}

const L1Buffer* L1Core::find_buffer(uint32_t start_addr) const {
    if (m_buffer_address_map.find(start_addr) != m_buffer_address_map.end()) {
        return m_buffer_address_map.at(start_addr).get();
    }
    return nullptr;
}

vector<const L1Buffer*> L1Core::get_all_buffers() const {
    vector<const L1Buffer*> all_buffers;
    for (const std::shared_ptr<L1Buffer> &buffer : m_all_buffers) {
        all_buffers.push_back(buffer.get());
    }
    return all_buffers;
}

void L1Core::add_buffer(const L1BufferType &buffer_type, const string &name, uint32_t start_addr, int consumed_size, int reserved_size) {
    log_assert(consumed_size <= reserved_size, "{}: Buffer {} is using more l1 memory than reserved ({} > {})", __FUNCTION__, name, consumed_size, reserved_size);
    log_assert(m_total_reserved_size + reserved_size <= m_l1_size, "{}: Total reserved size {} is greater than l1 size {} for physical core {}", __FUNCTION__, m_total_reserved_size + reserved_size, m_l1_size, m_physical_coord.str());
    log_assert(m_buffer_address_map.find(start_addr) == m_buffer_address_map.end(), "{}: Buffer already exists at address {}", __FUNCTION__, start_addr);
    std::shared_ptr<L1Buffer> new_buffer = std::make_shared<L1Buffer>(buffer_type, name, start_addr, consumed_size, reserved_size);
    m_all_buffers.push_back(new_buffer);
    m_buffer_address_map.insert({start_addr, new_buffer});
    if (consumed_size != buffer_size_undefined) {
        m_total_consumed_size += consumed_size;
    }
    if (reserved_size != buffer_size_undefined) {
        m_total_reserved_size += reserved_size;
    }
}

void L1Core::update_buffer_consumed_size(uint32_t start_addr, int new_consumed_size) {
    log_assert(m_buffer_address_map.find(start_addr) != m_buffer_address_map.end(), "{}: Buffer to update at address {} doesn't exist", __FUNCTION__, start_addr);
    std::shared_ptr<L1Buffer> &buffer = m_buffer_address_map.at(start_addr);
    if (buffer->has_consumed_size()) {
        m_total_consumed_size -= buffer->c_size();
    }
    if (new_consumed_size != buffer_size_undefined) {
        m_total_consumed_size += new_consumed_size;
    }
    buffer->update_consumed_size(new_consumed_size);
}

void L1Core::update_buffer_reserved_size(uint32_t start_addr, int new_reserved_size) {
    log_assert(m_buffer_address_map.find(start_addr) != m_buffer_address_map.end(), "{}: Buffer to update at address {} doesn't exist", __FUNCTION__, start_addr);
    std::shared_ptr<L1Buffer> &buffer = m_buffer_address_map.at(start_addr);
    if (buffer->has_reserved_size()) {
        m_total_reserved_size -= buffer->r_size();
    }
    if (new_reserved_size != buffer_size_undefined) {
        m_total_reserved_size += new_reserved_size;
    }
    log_assert(m_total_reserved_size <= m_l1_size, "{}: Total reserved size {} is greater than l1 size {} for physical core {}", __FUNCTION__, m_total_reserved_size, m_l1_size, m_physical_coord.str());
    buffer->update_reserved_size(new_reserved_size);
}

void L1Core::sort_buffers_and_check_overlap() {
    std::sort(m_all_buffers.begin(), m_all_buffers.end(), [](const std::shared_ptr<L1Buffer> &b1, const std::shared_ptr<L1Buffer> &b2) { return b1->start_addr() < b2->start_addr(); });
    for (int i = 0; i < m_all_buffers.size() - 1; i++) {
        const std::shared_ptr<L1Buffer> &curr_buffer = m_all_buffers[i];
        const std::shared_ptr<L1Buffer> &next_buffer = m_all_buffers[i + 1];
        log_assert(curr_buffer->end_addr() <= next_buffer->start_addr(), "L1 buffers {}, {} spanning regions {} - {}, {} - {} overlap!", 
            curr_buffer->name(), next_buffer->name(), curr_buffer->start_addr(), curr_buffer->end_addr(), next_buffer->start_addr(), next_buffer->end_addr());
    }
}

/*********************
 * L1 Graph Methods  
**********************/

L1Graph::L1Graph(const string &graph_name, int temporal_epoch, chip_id_t target_device, const unordered_map<string, core_descriptor> &cores_to_descriptors, uint32_t l1_size)
: m_temporal_epoch(temporal_epoch), m_target_device(target_device), m_graph_name(graph_name) {
    // initialize empty l1 cores for this graph
    // cores_to_descriptors contains all the worker cores of this graph, and their corresponding coordinates, ops etc.
    for (const auto &[physical_core_str, descriptor] : cores_to_descriptors) {
        const tt_cxy_pair physical_coord(target_device, descriptor.physical_core);
        const tt_cxy_pair logical_coord(target_device, descriptor.logical_core);
        std::shared_ptr<L1Core> l1_core = std::make_shared<L1Core>(physical_coord, logical_coord, descriptor.op_name, descriptor.op_type, l1_size);
        m_physical_l1_cores.insert({physical_coord, l1_core});
        m_logical_l1_cores.insert({logical_coord, l1_core});
    }
}

unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> &L1Graph::get_cores_by_coord_type(CoordType coord_type) {
    if (coord_type == CoordType::Physical) {
        return m_physical_l1_cores;
    }
    else if (coord_type == CoordType::Logical) {
        return m_logical_l1_cores;
    }
    else {
        log_fatal("Invalid coordinate type");
    }
}

void L1Graph::add_buffer_to_core(
    const tt_cxy_pair &core, 
    const L1BufferType &buffer_type, 
    const string &name, 
    uint32_t start_addr, 
    int consumed_size, 
    int reserved_size,
    CoordType coord_type
) {
    log_assert(m_target_device == core.chip, "{}: graph {} target device {} and core {} target device {} mismatch", __FUNCTION__, m_graph_name, m_target_device, core.str(), core.chip);
    unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> &l1_cores = get_cores_by_coord_type(coord_type);
    tt_xy_pair core_coord(core.x, core.y);
    // L1 profiler profiles for worker cores (cores that run tensix ops) only
    log_assert(l1_cores.find(core_coord) != l1_cores.end(), "{}: Core {} not found in graph {}", __FUNCTION__, core.str(), m_graph_name);
    l1_cores.at(core_coord)->add_buffer(buffer_type, name, start_addr, consumed_size, reserved_size);
}

void L1Graph::update_core_buffer_consumed_size(const tt_cxy_pair &core, uint32_t start_addr, int new_consumed_size, CoordType coord_type) {
    log_assert(m_target_device == core.chip, "{}: graph {} target device {} and core {} target device {} mismatch", __FUNCTION__, m_graph_name, m_target_device, core.str(), core.chip);
    unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> &l1_cores = get_cores_by_coord_type(coord_type);
    tt_xy_pair core_coord(core.x, core.y);
    // L1 profiler profiles for worker cores (cores that run tensix ops) only
    log_assert(l1_cores.find(core_coord) != l1_cores.end(), "{}: Core {} not found in graph {}", __FUNCTION__, core.str(), m_graph_name);
    l1_cores.at(core_coord)->update_buffer_consumed_size(start_addr, new_consumed_size);
}

void L1Graph::update_buffer_consumed_size_of_all_used_workers(uint32_t start_addr, int new_consumed_size) {
    for (auto &[core_coord, l1_core] : m_physical_l1_cores) {
        l1_core->update_buffer_consumed_size(start_addr, new_consumed_size);
    }
}

void L1Graph::update_core_buffer_reserved_size(const tt_cxy_pair &core, uint32_t start_addr, int new_reserved_size, CoordType coord_type) {
    log_assert(m_target_device == core.chip, "{}: graph {} target device {} and core {} target device {} mismatch", __FUNCTION__, m_graph_name, m_target_device, core.str(), core.chip);
    unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> &l1_cores = get_cores_by_coord_type(coord_type);
    tt_xy_pair core_coord(core.x, core.y);
    // L1 profiler profiles for worker cores (cores that run tensix ops) only
    log_assert(l1_cores.find(core_coord) != l1_cores.end(), "{}: Core {} not found in graph {}", __FUNCTION__, core.str(), m_graph_name);
    l1_cores.at(core_coord)->update_buffer_reserved_size(start_addr, new_reserved_size);
}

void L1Graph::add_buffer_to_all_used_workers(const L1BufferType &buffer_type, const string &name, uint32_t start_addr, int consumed_size, int reserved_size) {
    for (auto &[core_coord, l1_core] : m_physical_l1_cores) {
        l1_core->add_buffer(buffer_type, name, start_addr, consumed_size, reserved_size);
    }
}

void L1Graph::sort_buffers_and_check_overlap() {
    for (auto &[core_coord, l1_core] : m_physical_l1_cores) {
        l1_core->sort_buffers_and_check_overlap();
    }
}

const L1Buffer* L1Graph::find_buffer_in_core(const tt_cxy_pair &core, CoordType coord_type, uint32_t start_addr) const {
    if (coord_type == CoordType::Physical and m_physical_l1_cores.find(core) != m_physical_l1_cores.end()) {
        return m_physical_l1_cores.at(core)->find_buffer(start_addr);
    }
    else if (coord_type == CoordType::Logical and m_logical_l1_cores.find(core) != m_logical_l1_cores.end()) {
        return m_logical_l1_cores.at(core)->find_buffer(start_addr);
    }
    return nullptr;
}

/*********************
 * L1 Profiler Methods  
**********************/

void L1Profiler::add_graph(const string &graph_name, int temporal_epoch, chip_id_t target_device, const unordered_map<string, core_descriptor> &cores_to_descriptors) {
    if (m_graphs.find(graph_name) == m_graphs.end()) {
        m_graphs.emplace(graph_name, std::make_unique<L1Graph>(graph_name, temporal_epoch, target_device, cores_to_descriptors, m_l1_size));
    }
}

void L1Profiler::add_buffer_to_graph(
    const string& graph_name, 
    const tt_cxy_pair &core_coord, 
    const L1BufferType &buffer_type, 
    const string &buffer_name, 
    uint32_t start_addr, 
    int consumed_size, 
    int reserved_size,
    CoordType coord_type,
    L1ProfileStage stage
) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph name {} does not exist!", __FUNCTION__, graph_name);
    
    L1Graph* target_graph = m_graphs.at(graph_name).get();

    // if the stage at which the buffer is added to the graph is earlier than the profile stage of the graph, don't add the buffer
    if (target_graph->get_profile_stage() >= stage) {
        log_debug(tt::LogPerfInfra, "{}: not adding buffer to graph {} because graph profile stage is greater than buffer stage.", __FUNCTION__, graph_name);
        return;
    }

    target_graph->add_buffer_to_core(
        core_coord,
        buffer_type,
        buffer_name,
        start_addr,
        consumed_size,
        reserved_size,
        coord_type
    );
}

void L1Profiler::update_graph_buffer_consumed_size(
    const string &graph_name, 
    const tt_cxy_pair &core_coord, 
    uint32_t start_addr, 
    int new_consumed_size, 
    CoordType coord_type,
    L1ProfileStage stage
) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph name {} does not exist!", __FUNCTION__, graph_name);
    
    L1Graph* target_graph = m_graphs.at(graph_name).get();

    // if the stage of the graph is greater than the stage at which the buffer is being updated, don't update the buffer
    if (target_graph->get_profile_stage() >= stage) {
        log_debug(tt::LogPerfInfra, "{}: not updating buffer of graph {} because graph profile stage is greater than buffer stage.", __FUNCTION__, graph_name);
        return;
    }

    target_graph->update_core_buffer_consumed_size(
        core_coord,
        start_addr,
        new_consumed_size,
        coord_type
    );
}

void L1Profiler::broadcast_update_buffer_consumed_size_of_graph(const string &graph_name, uint32_t start_addr, int new_consumed_size, L1ProfileStage stage) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: graph {} not found", __FUNCTION__, graph_name);
    L1Graph* l1_graph = m_graphs.at(graph_name).get();
    if (l1_graph->get_profile_stage() >= stage) {
        log_debug(tt::LogPerfInfra, "{}: not adding buffer to graph because graph profile stage is greater than buffer stage.", __FUNCTION__, graph_name);
        return;
    }
    l1_graph->update_buffer_consumed_size_of_all_used_workers(start_addr, new_consumed_size);
}

void L1Profiler::update_graph_buffer_reserved_size(
    const string &graph_name, 
    const tt_cxy_pair &core_coord, 
    uint32_t start_addr, 
    int new_reserved_size, 
    CoordType coord_type,
    L1ProfileStage stage
) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph name {} does not exist!", __FUNCTION__, graph_name);
    
    L1Graph* target_graph = m_graphs.at(graph_name).get();

    // if the stage of the graph is greater than the stage at which the buffer is being updated, don't update the buffer
    if (target_graph->get_profile_stage() >= stage) {
        log_debug(tt::LogPerfInfra, "{}: not updating buffer of graph {} because graph profile stage is greater than buffer stage.", __FUNCTION__, graph_name);
        return;
    }

    target_graph->update_core_buffer_reserved_size(
        core_coord,
        start_addr,
        new_reserved_size,
        coord_type
    );
}

void L1Profiler::broadcast_add_buffer_all_used_workers(const string &graph_name, const L1BufferType &buffer_type, const string &buffer_name, uint32_t start_addr, int consumed_size, int reserved_size, L1ProfileStage stage) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: graph {} not found", __FUNCTION__, graph_name);
    L1Graph* l1_graph = m_graphs.at(graph_name).get();
    if (l1_graph->get_profile_stage() >= stage) {
        log_debug(tt::LogPerfInfra, "{}: not adding buffer to graph because graph profile stage is greater than buffer stage.", __FUNCTION__, graph_name);
        return;
    }
    l1_graph->add_buffer_to_all_used_workers(
        buffer_type,
        buffer_name,
        start_addr,
        consumed_size,
        reserved_size
    );
}

void L1Profiler::set_graph_profile_stage(const string &graph_name, L1ProfileStage stage) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph {} was never recorded.", __FUNCTION__, graph_name);
    m_graphs.at(graph_name)->set_profile_stage(stage);

}

L1ProfileStage L1Profiler::get_graph_stage(const string &graph_name) const {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph {} was never recorded.", __FUNCTION__, graph_name);
    return m_graphs.at(graph_name)->get_profile_stage();
}

void L1Profiler::sort_graph_buffers_and_check_overlap(const string &graph_name) {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph {} was never recorded.", __FUNCTION__, graph_name);
    m_graphs.at(graph_name)->sort_buffers_and_check_overlap();
}

void L1Profiler::sort_all_graph_buffers_and_check_overlap() {
    for (auto &[graph_name, l1_graph] : m_graphs) {
        l1_graph->sort_buffers_and_check_overlap();
    }
}

const L1Buffer* L1Profiler::find_buffer_in_graph(const string &graph_name, const tt_cxy_pair &core, CoordType coord_type, uint32_t start_addr) const {
    log_assert(m_graphs.find(graph_name) != m_graphs.end(), "{}: Graph {} was never recorded.", __FUNCTION__, graph_name);
    return m_graphs.at(graph_name)->find_buffer_in_core(core, coord_type, start_addr);
}

void L1Profiler::create_reports(const string &output_dir) const {
    const string metadata_key = "metadata";
    const string used_workers_key = "worker-cores";
    for (const auto &[graph_name, l1_graph] : m_graphs) {
        log_assert(l1_graph->get_profile_stage() == L1ProfileStage::Done, "{}: Expected profiling on graph to be done", __FUNCTION__);
        json graph_l1_report;
        const string graph_l1_report_path = output_dir + "/" + graph_name + ".json";
        graph_l1_report[metadata_key] = json::object();
        graph_l1_report[metadata_key]["graph-name"] = graph_name;
        graph_l1_report[metadata_key]["target-device"] = l1_graph->target_device();
        graph_l1_report[metadata_key]["arch-name"] = m_arch_name;
        graph_l1_report[metadata_key]["temporal-epoch"] = l1_graph->temporal_epoch();
        graph_l1_report[used_workers_key] = json::object();
        for (const auto &[phys_coord, l1_core] : l1_graph->get_cores_by_coord_type(CoordType::Physical)) {
            const string physical_core_str = get_core_str(l1_core->physical_coord());
            const string logical_core_str = get_core_str(l1_core->logical_coord());
            graph_l1_report[used_workers_key][physical_core_str] = json::object();
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"] = json::object();
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"]["logical-core-x-y"] = logical_core_str;
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"]["op-name"] = l1_core->op_name();
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"]["op-type"] = l1_core->op_type();
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"]["l1-size-bytes"] = m_l1_size;
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"]["total-reserved-size-bytes"] = l1_core->reserved_size_bytes();
            graph_l1_report[used_workers_key][physical_core_str]["core-attributes"]["total-consumed-size-bytes"] = l1_core->consumed_size_bytes();

            // Create reports for binary and data buffers
            graph_l1_report[used_workers_key][physical_core_str]["binary-buffers"] = json::array();
            graph_l1_report[used_workers_key][physical_core_str]["data-buffers"] = json::array();
            for (const L1Buffer* buffer : l1_core->get_all_buffers()) {
                const string buffer_name = buffer->name();
                const string buffer_category_name = buffer->category_name();
                json::object_t json_buffer = json::object();
                json_buffer["buffer-name"] = buffer_name;
                json_buffer["start-address"] = to_hex(buffer->start_addr(), 8);
                if (buffer->has_reserved_size()) {
                    json_buffer["reserved-size-bytes"] = buffer->r_size();
                }
                else {
                    json_buffer["reserved-size-bytes"] = "N/A";
                }
                if (buffer->has_consumed_size()) {
                    json_buffer["consumed-size-bytes"] = buffer->c_size();
                }
                else {
                    json_buffer["consumed-size-bytes"] = "N/A";
                }
                if (buffer->has_reserved_size() and buffer->has_consumed_size()) {
                    json_buffer["percent-consumed"] = buffer->get_percent_consumed();
                }
                else {
                    json_buffer["percent-consumed"] = "N/A";
                }
                graph_l1_report[used_workers_key][physical_core_str][buffer_category_name].emplace_back(std::move(json_buffer));
            }
        }
        std::ofstream graph_report_file(graph_l1_report_path);
        graph_report_file << std::setw(4) << graph_l1_report;
        graph_report_file.flush();
        graph_report_file.close();
    }
}

/**************************
 * Memory Profiler Methods  
***************************/

MemoryProfiler::MemoryProfiler(const unordered_map<chip_id_t, buda_SocDescriptor> &sdesc_per_chip, bool l1_profiler_en): m_profile_l1(l1_profiler_en) {   
    if (l1_profiler_en) {
        for (const auto &[chip_id, sdesc] : sdesc_per_chip) {
            const string sdesc_arch = get_arch_str(sdesc.arch);
            if (m_arch_name == "") {
                m_arch_name = sdesc_arch;
            }
            else {
                log_assert(m_arch_name == sdesc_arch, "Architecture mismatch between cores: {} and {}", m_arch_name, sdesc_arch);
            }
            if (m_l1_size == 0) {
                m_l1_size = sdesc.worker_l1_size;
            }
            else {
                log_assert(m_l1_size == sdesc.worker_l1_size, "L1 size mismatch between cores: {} and {}", m_l1_size, sdesc.worker_l1_size);
            }
        }
        m_l1_profiler = std::make_unique<L1Profiler>(m_arch_name, m_l1_size);
    }
}

void MemoryProfiler::add_graph(const buda_SocDescriptor &sdesc, const tt_digraph &graph, int temporal_epoch_id) {
    chip_id_t target_device = graph.my_graph_info.target_device;
    update_chip_id_to_sdesc(target_device, sdesc);
    const string &graph_name = graph.my_graph_info.name;
    update_graph_wrappers(graph_name, graph, temporal_epoch_id);
    update_graph_used_workers(graph_name, m_graph_wrappers_graph_name.at(graph_name)->core_to_descriptor);
    if (m_profile_l1) {
        m_l1_profiler->add_graph(graph_name, temporal_epoch_id, target_device, m_graph_wrappers_graph_name.at(graph_name)->core_to_descriptor);
    }
}

void MemoryProfiler::update_chip_id_to_sdesc(chip_id_t chip_id, const buda_SocDescriptor &soc_descriptor) {
    if (m_chip_id_to_sdesc.find(chip_id) == m_chip_id_to_sdesc.end()) {
        log_assert(m_arch_name == get_arch_str(soc_descriptor.arch), "New soc descriptor has different architecture than memory profiler");
        log_assert(m_l1_size == soc_descriptor.worker_l1_size, "New soc descriptor has different l1 size than memory profiler");
        m_chip_id_to_sdesc.emplace(chip_id, std::make_shared<buda_SocDescriptor>(soc_descriptor));
    }
}

void MemoryProfiler::update_graph_wrappers(const string &graph_name, const tt_digraph &graph, int temporal_epoch_id) {
    // if the graph doesn't exist, create necessary data structures that describe it
    // and populate both maps with its name, temporal epoch and target device
    if (m_graph_wrappers_graph_name.find(graph_name) == m_graph_wrappers_graph_name.end()) {
        const int target_device = graph.my_graph_info.target_device;
        log_assert(m_chip_id_to_sdesc.find(target_device) != m_chip_id_to_sdesc.end(), "soc descriptor does not exist for chip id {}", target_device);
        std::shared_ptr<tt_perf_digraph_wrapper> new_graph_wrapper = std::make_shared<tt_perf_digraph_wrapper>();
        new_graph_wrapper->graph = graph;
        unordered_map<string, core_descriptor> core_to_descriptor = postprocess::populate_cores_to_ops_for_graph(
                                                                            graph.my_graph_info,
                                                                            *m_chip_id_to_sdesc.at(target_device));
        new_graph_wrapper->core_to_descriptor = core_to_descriptor;
        m_graph_wrappers_graph_name.insert({graph_name, new_graph_wrapper});
        if (m_graph_wrappers_temporal_epoch_device.find(temporal_epoch_id) == m_graph_wrappers_temporal_epoch_device.end()) {
            m_graph_wrappers_temporal_epoch_device.emplace(temporal_epoch_id, unordered_map<chip_id_t, std::shared_ptr<tt_perf_digraph_wrapper>>());
        }

        // shouldn't be trying to add a new graph to a temporal epoch device if there is already a graph running on that device
        log_assert(m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).find(target_device) == m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).end(), 
            "{}: Can't add new graph to temporal epoch device: temporal epoch {} already has graph {} running on device {}", __FUNCTION__, 
            temporal_epoch_id, m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).at(target_device)->graph.my_graph_info.name, target_device);
        
        m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).insert({target_device, new_graph_wrapper});
    }
}

void MemoryProfiler::update_graph_used_workers(const string &graph_name, const unordered_map<string, core_descriptor> &cores_to_descriptors) {
    log_assert((m_graph_physical_used_workers.find(graph_name) == m_graph_physical_used_workers.end()) == (m_graph_logical_used_workers.find(graph_name) == m_graph_logical_used_workers.end()), 
        "{}: Physical logical worker cores out of sync for graph {}", __FUNCTION__, graph_name);

    if (m_graph_logical_used_workers.find(graph_name) == m_graph_logical_used_workers.end()) {
        m_graph_logical_used_workers.emplace(graph_name, unordered_set<tt_xy_pair>());
        unordered_set<tt_xy_pair> &graph_logical_workers = m_graph_logical_used_workers.at(graph_name);
        for (const auto &[core_str, descriptor] : cores_to_descriptors) {
            log_assert(graph_logical_workers.find(descriptor.logical_core) == graph_logical_workers.end(), "{}: Logical core {} already exists in graph {}", __FUNCTION__, descriptor.logical_core.str(), graph_name);
            graph_logical_workers.insert(descriptor.logical_core);
        }
    }
    if (m_graph_physical_used_workers.find(graph_name) == m_graph_physical_used_workers.end()) {
        m_graph_physical_used_workers.emplace(graph_name, unordered_set<tt_xy_pair>());
        unordered_set<tt_xy_pair> &graph_physical_workers = m_graph_physical_used_workers.at(graph_name);
        for (const auto &[core_str, descriptor] : cores_to_descriptors) {
            log_assert(graph_physical_workers.find(descriptor.physical_core) == graph_physical_workers.end(), "{}: Physical core {} already exists in graph {}", __FUNCTION__, descriptor.physical_core.str(), graph_name);
            graph_physical_workers.insert(descriptor.physical_core);
        }
    }
    log_assert(m_graph_physical_used_workers.size() == m_graph_logical_used_workers.size(), "{}: Physical and logical worker cores size out of sync for graph {}", __FUNCTION__, graph_name);
}

const string &MemoryProfiler::get_graph_name(int temporal_epoch_id, chip_id_t device_id) const {
    log_assert(temporal_epoch_device_exists(temporal_epoch_id, device_id), "{}: graph on device {} at temporal epoch {} not recorded!", __FUNCTION__, device_id, temporal_epoch_id);
    return m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).at(device_id)->graph.my_graph_info.name;
}

bool MemoryProfiler::is_used_worker(const string &graph_name, const tt_xy_pair &core_coord, CoordType coord_type) const {
    if (coord_type == CoordType::Physical) {
        log_assert(m_graph_physical_used_workers.find(graph_name) != m_graph_physical_used_workers.end(), "{}: Graph {} not recorded", __FUNCTION__, graph_name);
        const unordered_set<tt_xy_pair> &graph_physical_workers = m_graph_physical_used_workers.at(graph_name);
        return graph_physical_workers.find(core_coord) != graph_physical_workers.end();
    }
    else if (coord_type == CoordType::Logical) {
        log_assert(m_graph_logical_used_workers.find(graph_name) != m_graph_logical_used_workers.end(), "{}: Graph {} not recorded", __FUNCTION__, graph_name);
        const unordered_set<tt_xy_pair> &graph_logical_workers = m_graph_logical_used_workers.at(graph_name);
        return graph_logical_workers.find(core_coord) != graph_logical_workers.end();
    }
    else {
        log_fatal("Invalid coordinate type");
    }
}

bool MemoryProfiler::is_used_worker(int temporal_epoch_id, const tt_cxy_pair &core, CoordType coord_type) const {
    const string &graph_name = get_graph_name(temporal_epoch_id, core.chip);
    return is_used_worker(graph_name, tt_xy_pair(core.x, core.y), coord_type);
}

void MemoryProfiler::add_buffer_to_graph_l1(
    const string &graph_name, 
    const tt_cxy_pair &core_coord, 
    const L1BufferType &buffer_type, 
    const string &buffer_name, 
    uint32_t start_addr, 
    int consumed_size, 
    int reserved_size,
    CoordType coord_type,
    L1ProfileStage stage
) {
    if (!m_profile_l1) return;
    log_assert(m_graph_wrappers_graph_name.find(graph_name) != m_graph_wrappers_graph_name.end(), "{}: Graph {} not recorded");
    // FIXME: Comparing buffer names is currently a workaround
    // Handle the special case for extra overlay buffer, and merge it into the default overlay buffer
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    if (buffer_name == perf::extra_overlay_buffer_name) {
        const perf::L1Buffer* default_overlay_blob_buffer = find_buffer_in_graph_l1(graph_name, core_coord, coord_type, l1_mem::address_map::OVERLAY_BLOB_BASE);
        log_assert(default_overlay_blob_buffer != nullptr, "Default overlay blob buffer not found in memory profiler");
        log_assert(default_overlay_blob_buffer->r_size() != perf::buffer_size_undefined, "Initial overlay blob size undefined");
        int new_overlay_reserved_size = default_overlay_blob_buffer->r_size() + reserved_size;
        update_buffer_reserved_size_l1(graph_name, core_coord, default_overlay_blob_buffer->start_addr(), new_overlay_reserved_size, coord_type, stage);
        return;
    }
    m_l1_profiler->add_buffer_to_graph(
        graph_name, 
        core_coord,
        buffer_type,
        buffer_name,
        start_addr,
        consumed_size,
        reserved_size,
        coord_type,
        stage
    );
}

void MemoryProfiler::add_buffer_to_graph_l1(
    int temporal_epoch_id, 
    const tt_cxy_pair &core_coord, 
    const L1BufferType &buffer_type, 
    const string &buffer_name, 
    uint32_t start_addr, 
    int consumed_size, 
    int reserved_size,
    CoordType coord_type,
    L1ProfileStage stage
) {
    if (!m_profile_l1) return;
    chip_id_t device_id = core_coord.chip;
    const string &graph_name = get_graph_name(temporal_epoch_id, device_id);
    add_buffer_to_graph_l1(graph_name, core_coord, buffer_type, buffer_name, start_addr, consumed_size, reserved_size, coord_type, stage);
}


void MemoryProfiler::broadcast_add_buffer_l1(
    const L1BufferType &buffer_type, 
    const string &buffer_name, 
    const uint32_t start_addr,
    int consumed_size, 
    int reserved_size,
    L1ProfileStage stage
) {
    if (!m_profile_l1) return;
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    for (const auto &[graph_name, graph_wrapper] : m_graph_wrappers_graph_name) {
        m_l1_profiler->broadcast_add_buffer_all_used_workers(
            graph_name,
            buffer_type,
            buffer_name,
            start_addr,
            consumed_size,
            reserved_size,
            stage
        );
    }
}

void MemoryProfiler::update_buffer_consumed_size_l1(
    const string &graph_name, 
    const tt_cxy_pair &core_coord, 
    uint32_t start_addr, 
    int new_consumed_size, 
    CoordType coord_type,
    L1ProfileStage stage
) {
    if (!m_profile_l1) return;
    log_assert(m_graph_wrappers_graph_name.find(graph_name) != m_graph_wrappers_graph_name.end(), "{}: Graph {} not recorded");
    if (!is_used_worker(graph_name, core_coord, coord_type)) {
        log_debug(tt::LogPerfInfra, "{}: Not updating buffer consumed size for graph {} core {} because it is not a used worker core", __FUNCTION__, graph_name, core_coord.str());
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    m_l1_profiler->update_graph_buffer_consumed_size(
        graph_name, 
        core_coord,
        start_addr,
        new_consumed_size,
        coord_type, 
        stage
    );
};

void MemoryProfiler::broadcast_update_buffer_consumed_size_l1(
    uint32_t start_addr,
    int new_consumed_size,
    L1ProfileStage stage
) {
    if (!m_profile_l1) return;
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    for (const auto &[graph_name, graph_wrapper] : m_graph_wrappers_graph_name) {
        m_l1_profiler->broadcast_update_buffer_consumed_size_of_graph(
            graph_name, 
            start_addr,
            new_consumed_size,
            stage
        );
    }
}

void MemoryProfiler::update_buffer_reserved_size_l1(
    const string &graph_name, 
    const tt_cxy_pair &core_coord, 
    uint32_t start_addr, 
    int new_reserved_size, 
    CoordType coord_type,
    L1ProfileStage stage
) {
    if (!m_profile_l1) return;
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    m_l1_profiler->update_graph_buffer_reserved_size(
        graph_name, 
        core_coord,
        start_addr,
        new_reserved_size,
        coord_type, 
        stage
    );
};

void MemoryProfiler::update_graph_profile_stage_l1(const string &graph_name, L1ProfileStage stage, bool force_update) {
    if (!m_profile_l1) return;
    log_assert(m_graph_wrappers_graph_name.find(graph_name) != m_graph_wrappers_graph_name.end(), "{}: Graph {} not recorded");
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    if (!force_update and m_l1_profiler->get_graph_stage(graph_name) >= stage) {
        return;
    }
    m_l1_profiler->set_graph_profile_stage(graph_name, stage);
}

void MemoryProfiler::update_profile_stage_all_graphs_l1(L1ProfileStage stage, bool force_update) {
    if (!m_profile_l1) return;
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    for (const auto &[graph_name, graph_wrapper] : m_graph_wrappers_graph_name) {
        if (!force_update and m_l1_profiler->get_graph_stage(graph_name) >= stage) {
            continue;
        }
        m_l1_profiler->set_graph_profile_stage(graph_name, stage);
    }
}

void MemoryProfiler::update_temporal_epoch_profile_stage_l1(int temporal_epoch_id, L1ProfileStage stage, bool force_update) {
    if (!m_profile_l1) return;
    log_assert(m_graph_wrappers_temporal_epoch_device.find(temporal_epoch_id) != m_graph_wrappers_temporal_epoch_device.end(), 
        "{}: temporal epoch {} does not exist!", temporal_epoch_id);
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    for (const auto &[device_id, graph_wrapper] : m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id)) {
        const string graph_name = graph_wrapper->graph.my_graph_info.name;
        if (!force_update and m_l1_profiler->get_graph_stage(graph_name) >= stage) {
            continue;
        }
        m_l1_profiler->set_graph_profile_stage(graph_name, stage);
    }
}

L1ProfileStage MemoryProfiler::get_graph_profile_stage_l1(const string &graph_name) {
    if (!m_profile_l1) return L1ProfileStage::Uninitialized;
    log_assert(m_graph_wrappers_graph_name.find(graph_name) != m_graph_wrappers_graph_name.end(), "{}: Graph {} not recorded");
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    return m_l1_profiler->get_graph_stage(graph_name);
}

L1ProfileStage MemoryProfiler::get_graph_profile_stage_l1(int temporal_epoch_id, chip_id_t device_id) {
    if (!m_profile_l1) return L1ProfileStage::Uninitialized;
    const string &graph_name = get_graph_name(temporal_epoch_id, device_id);
    return get_graph_profile_stage_l1(graph_name);
}

void MemoryProfiler::sort_graph_buffers_and_check_overlap_l1(const string &graph_name) {
    if (!m_profile_l1) return;
    log_assert(m_graph_wrappers_graph_name.find(graph_name) != m_graph_wrappers_graph_name.end(), "{}: Graph {} not recorded");
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    m_l1_profiler->sort_graph_buffers_and_check_overlap(graph_name);
}

void MemoryProfiler::sort_all_graph_buffers_and_check_overlap_l1() {
    if (!m_profile_l1) return;
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    m_l1_profiler->sort_all_graph_buffers_and_check_overlap();
}

const L1Buffer* MemoryProfiler::find_buffer_in_graph_l1(const string &graph_name, const tt_cxy_pair &core, CoordType coord_type, uint32_t start_addr) {
    if (!m_profile_l1) return nullptr;
    chip_id_t device_id = core.chip;
    std::lock_guard<std::recursive_mutex> lock(m_l1_profiler_mutex);
    return m_l1_profiler->find_buffer_in_graph(graph_name, core, coord_type, start_addr);
}

void MemoryProfiler::create_reports_l1(const string &l1_output_dir) const {
    if (!m_profile_l1) return;
    m_l1_profiler->create_reports(l1_output_dir);
}

}