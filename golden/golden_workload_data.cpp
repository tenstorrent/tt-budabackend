// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "golden_workload_data.hpp"

#include "common/mem_lib.hpp"
#include "common/size_lib.hpp"
#include "utils/logger.hpp"

namespace tt::golden {
////////////////////////////////
// golden_workload_data
////////////////////////////////
void golden_workload_data::populate_temporal_graphs_from_parser(const netlist_parser &parser) {
    for (int e = 0; e < parser.get_number_of_temporal_graphs(); e++) {
        const auto &graph_names = parser.get_graphs_of_temporal_graph(e);
        // Get graphs
        std::string temporal_graph_name = "temporal_graph_" + to_string(e);
        int temporal_graph_input_count = -1;
        std::unordered_map<int, bool> device_used = {};
        std::map<string, tt_op_info> temporal_graph_op_map;
        unsigned int total_ops = 0;
        // Process subgraphs
        map_temporal_graph_list_of_subgraphs.insert({temporal_graph_name, {}});
        for (const auto &graph_name : graph_names) {
            if (temporal_graph_input_count == -1) {
                temporal_graph_input_count = parser.graph_map.at(graph_name).input_count;
            } else {
                log_assert(
                    temporal_graph_input_count == parser.graph_map.at(graph_name).input_count,
                    "Temporal sub-graph={} must have same input-count as rest of the temporal graph",
                    graph_name);
            }
            int target_device = parser.graph_map.at(graph_name).target_device;
            log_assert(
                device_used.find(target_device) == device_used.end(),
                "Temporal sub-graph={} cannot use the same device={} again",
                graph_name,
                target_device);
            device_used.insert({target_device, true});
            total_ops += parser.graph_map.at(graph_name).op_map.size();
            temporal_graph_op_map.insert(
                parser.graph_map.at(graph_name).op_map.begin(), parser.graph_map.at(graph_name).op_map.end());
            map_temporal_graph_list_of_subgraphs.at(temporal_graph_name).push_back(graph_name);
            map_of_subgraphs_to_temporal_graph.insert({graph_name, temporal_graph_name});
        }
        log_assert(
            total_ops == temporal_graph_op_map.size(),
            "Ops within all sub-graphs of a temporal graph must be unique");

        tt_graph_info new_temporal_graph(
            {.name = temporal_graph_name,
             .target_device = 0,
             .input_count = temporal_graph_input_count,
             .op_map = temporal_graph_op_map});
        populate_graph(new_temporal_graph, parser.device_info.arch, parser);
    }
}
void golden_workload_data::populate_graphs_from_parser(const netlist_parser &parser) {
    populate_temporal_graphs_from_parser(parser);
    // Add all graphs as is
    for (const std::pair<const string, tt_graph_info>& graphs_it : parser.graph_map) {
        if (map_of_subgraphs_to_temporal_graph.find(graphs_it.first) == map_of_subgraphs_to_temporal_graph.end()) {
            populate_graph(graphs_it.second, parser.device_info.arch, parser);
        }
    }
}

bool golden_workload_data::set_and_check_execution_status_of_graph(const std::string &graph_name) {
    bool should_execute = is_last_graph_to_execute(graph_name);
    set_execution_status_if_subgraph(graph_name);
    return should_execute;
}
std::string golden_workload_data::get_temporal_graph(const std::string &graph_name) {
    if (map_of_subgraphs_to_temporal_graph.find(graph_name) != map_of_subgraphs_to_temporal_graph.end()) {
        return map_of_subgraphs_to_temporal_graph.at(graph_name);
    } else {
        return graph_name;
    }
}
bool golden_workload_data::is_subgraph(const std::string &graph_name) {
    return map_of_subgraphs_to_temporal_graph.find(graph_name) != map_of_subgraphs_to_temporal_graph.end();
}

void golden_workload_data::set_execution_status_if_subgraph(const std::string &graph_name) {
    if (is_subgraph(graph_name)) {
        if (map_of_subgraphs_to_execution_status.find(graph_name) == map_of_subgraphs_to_execution_status.end()) {
            map_of_subgraphs_to_execution_status.insert({graph_name, true});
        } else {
            if (map_of_subgraphs_to_execution_status.at(graph_name)) {
                log_fatal(
                    "Subgraph={} has been executed already, and executing again before execution status was cleared, "
                    "likely missing an execute statement, or trying to use subgraph standalone which is not supported",
                    graph_name);
            }
            map_of_subgraphs_to_execution_status.at(graph_name) = true;
        }
    }
}
void golden_workload_data::clear_all_temporal_graph_execution_status() {
    map_of_subgraphs_to_execution_status.clear();
    saved_temporal_graph_map_of_queue_settings.clear();
}
void golden_workload_data::clear_temporal_graph_execution_status(const std::string &graph_name) {
    string temporal_graph_name = get_temporal_graph(graph_name);
    for (const auto &subgraph_name : map_temporal_graph_list_of_subgraphs.at(temporal_graph_name)) {
        if (map_of_subgraphs_to_execution_status.find(subgraph_name) == map_of_subgraphs_to_execution_status.end()) {
            log_fatal(
                "Subgraph={} of temporal_graph={} doesn't have execution status, likely missing an execute statement",
                subgraph_name,
                temporal_graph_name);
        }
        map_of_subgraphs_to_execution_status.at(subgraph_name) = false;
    }
    if (saved_temporal_graph_map_of_queue_settings.find(temporal_graph_name) !=
        saved_temporal_graph_map_of_queue_settings.end()) {
        saved_temporal_graph_map_of_queue_settings.at(temporal_graph_name).clear();
    }
}
bool golden_workload_data::is_last_graph_to_execute(const std::string &graph_name) {
    bool result = false;
    if (is_subgraph(graph_name)) {
        const auto &list_of_subgraphs = map_temporal_graph_list_of_subgraphs.at(get_temporal_graph(graph_name));
        unsigned int number_subgraphs_executed = 0;
        for (const auto &subgraph_name : list_of_subgraphs) {
            if (map_of_subgraphs_to_execution_status.find(subgraph_name) !=
                map_of_subgraphs_to_execution_status.end()) {
                if (map_of_subgraphs_to_execution_status.at(subgraph_name)) {
                    number_subgraphs_executed++;
                }
            }
        }
        result = number_subgraphs_executed == (list_of_subgraphs.size() - 1);
    } else {
        result = true;
    }
    return result;
}

void golden_workload_data::save_queue_setting_for_temporal_graph(
    const std::string &graph_name, const tt::golden::golden_queue_setting &queue_setting) {
    string temporal_graph_name = this->get_temporal_graph(graph_name);
    saved_temporal_graph_map_of_queue_settings.insert({temporal_graph_name, {}});
    auto &saved_queue_settings = saved_temporal_graph_map_of_queue_settings.at(temporal_graph_name);
    if (saved_queue_settings.find(queue_setting.name) == saved_queue_settings.end()) {
        saved_queue_settings.insert({queue_setting.name, queue_setting});
    } else if (saved_queue_settings.at(queue_setting.name) != queue_setting) {
        log_fatal(
            "There is an overlap in queue settings for graph={} which maps to temporal graph={} regarding queue={} - "
            "but the queue settings are not identical,"
            "which is currently not supported",
            graph_name,
            temporal_graph_name,
            queue_setting.name);
    }
}

std::unordered_map<std::string, tt::golden::golden_queue_setting>
golden_workload_data::get_queue_settings_for_temporal_graph(const std::string &graph_name) {
    string temporal_graph_name = get_temporal_graph(graph_name);
    if (saved_temporal_graph_map_of_queue_settings.find(temporal_graph_name) !=
        saved_temporal_graph_map_of_queue_settings.end()) {
        return saved_temporal_graph_map_of_queue_settings.at(temporal_graph_name);
    }
    return {};
}

void golden_workload_data::populate_graph(
    const tt_graph_info &graph_info, const ARCH arch, const netlist_parser &parser) {
    graphs[graph_info.name] = golden_digraph(graph_info, arch, m_config.en_quantize_golden);
    graphs[graph_info.name].add_all_nodes(queues, graphs[graph_info.name].my_graph_info);
    graphs[graph_info.name].connect_nodes();
    if (m_config.dump_graphs) {
        graphs[graph_info.name].dump_graphviz(m_config.output_dir + "/graphviz/golden/", graph_info.name);
    }
    graphs[graph_info.name].create_ops(parser.fused_ops_op_map);
}

void golden_workload_data::populate_queues_from_parser(const netlist_parser &parser) {
    for (const std::pair<const string, tt_queue_info>& queues_it : parser.queue_map) {
        queues[queues_it.second.name].my_queue_info = queues_it.second;
        if (queues_it.second.type == IO_TYPE::Queue) {
            queues[queues_it.second.name].my_io = static_pointer_cast<tt_io<std::shared_ptr<tt_tensor>>>(
                std::make_shared<tt_queue>(queues_it.second.entries, queues_it.second, queues_it.second.name));
        } else {
            queues[queues_it.second.name].my_io = static_pointer_cast<tt_io<std::shared_ptr<tt_tensor>>>(
                std::make_shared<tt_ram>(queues_it.second.entries, queues_it.second, queues_it.second.name));
        }
    }
}
void golden_workload_data::allocate_static_queues() {
    for (const auto &queue_it : this->queues) {
        if (is_static_queue(queue_it.first) and not_allocated(queue_it.first)) {
            // Static queues only need to be allocated if it isn't already.
            allocate_queue(queue_it.first);
        }
    }
}

bool golden_workload_data::not_allocated(const std::string &queue_name) {
    return allocated_queues.find(queue_name) == allocated_queues.end();
}

void golden_workload_data::allocate_queue(const std::string &queue_name) {
    log_assert(
        allocated_queues.find(queue_name) == allocated_queues.end(),
        "queue={} is already allocated or a static queue -- need to deallocate before allocating again",
        queue_name);
    std::vector<tt_address_range> address_ranges = get_allocation_address_ranges_for_queue(queue_name);
    // initialize allocations
    allocated_queues.insert({queue_name, {}});
    for (const auto &address_range : address_ranges) {
        // Check if this overlaps with any existing allocations
        for (const auto &allocated_queue_it : allocated_queues) {
            for (const auto &allocated_address_range : allocated_queue_it.second) {
                log_assert(
                    (not is_address_range_overlapping(address_range, allocated_address_range)) or
                        is_aliased_io(queue_name, allocated_queue_it.first),
                    "a0={} cannot be allocated due to overlapping address range with a1={} -- \na0={}\na1={} unless "
                    "its an aliased queue (exactly same address + size)",
                    queue_name,
                    allocated_queue_it.first,
                    address_range,
                    allocated_address_range);
            }
        }
        allocated_queues.at(queue_name).push_back(address_range);
    }

    this->queues[queue_name].my_io->clear();
}
void golden_workload_data::deallocate_queue(const std::string &queue_name) {
    log_assert(
        allocated_queues.find(queue_name) != allocated_queues.end(),
        "Cannot find queue={} to deallocate",
        queue_name);
    allocated_queues.erase(queue_name);
}

bool golden_workload_data::is_address_range_overlapping(tt_address_range a1, tt_address_range a2) {
    // if the channels overlap then we check address
    // We assume addresses are all formed nicely (base <= end)
    // then we can test if it overlaps if the a1.base <= a2.end and a1.end >= a2.base
    return (a1.device == a2.device) and (a1.channel == a2.channel) and
           ((a1.base_byte_address <= a2.end_byte_address) and (a1.end_byte_address >= a2.base_byte_address));
}
bool golden_workload_data::is_address_same(tt_address_range a1, tt_address_range a2) {
    return (a1.device == a2.device) and (a1.channel == a2.channel) and
           ((a1.base_byte_address == a2.end_byte_address) and (a1.end_byte_address == a2.base_byte_address));
}
void golden_workload_data::populate_queues_info_and_attributes() {
    parse_queue_producer_info();
    parse_queue_consumer_info();
    parse_dynamic_queue_info();
    parse_e2e_queue_info();
    parse_aliased_queue_info();
}

void golden_workload_data::parse_queue_producer_info() {
    std::unordered_map<std::string, std::vector<std::string>> op_to_qs_map;

    for (auto &queue_it : queues) {
        tt_queue_info &queue_info = queue_it.second.my_queue_info;
        if (!netlist_parser::is_queue_fed_by_op(queue_info)) {
            // host inputs are always in row-major scan, tilized format
            tt_queue_producer_info_t &prod_info = queue_it.second.my_producer_info;
            prod_info.is_tilized = true;
            prod_info.ublock_scan = Dim::R;
        } else {
            // device outputs to queue name map for later processing
            // if (op_to_qs_map.find(queue_info.input) == op_to_qs_map.end())
            //     op_to_qs_map.insert({queue_info.input, {queue_info.name}});
            // else
            op_to_qs_map[queue_info.input].push_back(queue_info.name);
        }
    }
    for (auto &graph_it : graphs) {
        std::map<string, tt_op_info> &op_map = graph_it.second.my_graph_info.op_map;
        for (auto &op_it : op_map) {
            tt_op_info &op_info = op_it.second;
            if (op_to_qs_map.find(op_info.name) != op_to_qs_map.end()) {
                for (std::string queue : op_to_qs_map.at(op_info.name)) {
                    tt_queue_producer_info_t &prod_info = queues.at(queue).my_producer_info;
                    prod_info.is_tilized = !op_info.untilize_output;
                    prod_info.ublock_scan = op_info.output_dim.ublock_order;
                    prod_info.graphs = {graph_it.first};
                }
                op_to_qs_map.erase(op_info.name);
            }
        }
    }
    log_assert(op_to_qs_map.size() == 0, "Queue remaining without input ops implemented!");
}

void golden_workload_data::parse_queue_consumer_info() {
    for (const auto &program : programs) {
        for (const auto &instruction : program.second.get_program_trace()) {
            if (instruction.opcode == INSTRUCTION_OPCODE::Execute) {
                for (const auto &setting : instruction.queue_settings) {
                    queues.at(setting.name).my_consumer_info.programs.insert(program.first);
                    queues.at(setting.name).my_consumer_info.graphs.insert(instruction.graph_name);
                }
            }
        }
    }
}
// Mimics what the host should do if it is re-running netlist.
// FIXME: we should only update wr pointers for input queues and global rd pointers for output queues
void golden_workload_data::reset_queues() {
    // Ensure all the queues are reset
    for (auto &queue_it : this->queues) {
        string queue_name = queue_it.first;
        tt_queue_wrap &queue_wrap = queue_it.second;
        queue_wrap.my_io->clear();
    }
}

std::shared_ptr<std::vector<uint32_t>> golden_workload_data::allocate_untilized_memory(
    std::string q_name, int num_entries) {
    tt_queue_info &q_info = queues.at(q_name).my_queue_info;
    auto queue_vector = std::make_shared<std::vector<uint32_t>>(num_entries * get_tensor_size_in_elements(q_info));
    tt::mem::host_shared_memory.insert({reinterpret_cast<void *>(queue_vector->data()), queue_vector});
    allocated_untilized_memories.push_back(reinterpret_cast<void *>(queue_vector->data()));
    log_trace(tt::LogGolden, "Allocating {} for q_name={}", reinterpret_cast<intptr_t>(queue_vector->data()), q_name);
    return queue_vector;
}

void golden_workload_data::deallocate_untilized_memory(void *ptr) {
    if (tt::mem::host_shared_memory.find(ptr) != tt::mem::host_shared_memory.end()) {
        log_trace(
            tt::LogGolden,
            "Deallocating {} -- shared_ptr use_count={}",
            reinterpret_cast<intptr_t>(ptr),
            tt::mem::host_shared_memory.at(ptr).use_count());
        log_assert(
            tt::mem::host_shared_memory.at(ptr).use_count() == 1,
            "Should only deallocate host shared memory if there is only 1 user left");
        tt::mem::host_shared_memory.erase(ptr);
    } else {
        log_trace(
            tt::LogGolden,
            "Skip deallocating {}, either it's already freed or was never allocated",
            reinterpret_cast<intptr_t>(ptr));
    }
}
}  // namespace tt::golden

