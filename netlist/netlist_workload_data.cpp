// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_workload_data.hpp"

#include "common/tt_parallel_for.h"
#include "common/size_lib.hpp"
#include "device/cpuset_lib.hpp"
#include "utils/logger.hpp"
#include "netlist_utils.hpp"
// #include "common/buda_soc_descriptor.h"

namespace tt {
////////////////////////////////
// netlist_workload_data
////////////////////////////////
void netlist_workload_data::populate_graphs_from_parser(const netlist_parser &parser) {
    for (const auto &graphs_it : parser.graph_map) {
        const tt_graph_info &graph_info = graphs_it.second;
        graphs[graph_info.name] = tt_digraph(graph_info);
    }
    tt::parallel_for(graphs.begin(), graphs.end(), [&](auto& graph) {
        populate_graph(graph.second, parser);
    }, tt::cpuset::get_allowed_num_threads());
    graph_order = parser.graph_order;
}

void netlist_workload_data::populate_programs_from_parser(const netlist_parser &parser) {
    for (const std::pair<string, tt_program_info> &program_it : parser.program_map) {
        programs.insert({program_it.first, netlist_program(program_it.first, program_it.second.program_trace)});
    }
    program_order = parser.program_order;
}

void netlist_workload_data::populate_graph(tt_digraph &graph, const netlist_parser &parser) {
    graph.add_all_nodes(queues, graph.my_graph_info);
    graph.connect_nodes();
    graph.create_ops(parser.fused_ops_op_map);
    if (m_config.dump_graphs) {
        graph.dump_graphviz(m_config.output_dir + "/graphviz/", graph.my_graph_info.name);
    }
}

void netlist_workload_data::populate_queues_from_parser(const netlist_parser &parser) {
    for (const std::pair<string, tt_queue_info> &queues_it : parser.queue_map) {
        queues[queues_it.second.name].my_queue_info = queues_it.second;
        queues[queues_it.second.name].my_io = static_pointer_cast<tt_io<std::shared_ptr<tt_tensor>>>(
            std::make_shared<tt_queue>(queues_it.second.entries, queues_it.second, queues_it.second.name));
    }
}

// Return set of unique target_device ids used by workload (queues, graphs). Ignore target_device
// from host queues since it does not refer to TT device.
std::set<chip_id_t> netlist_workload_data::compute_target_device_ids() {
    std::set<chip_id_t> target_devices_set;

    for (auto &queue_it : queues) {
        tt_queue_info &queue_info = queue_it.second.my_queue_info;
        if (queue_info.loc != QUEUE_LOCATION::HOST) {
            target_devices_set.insert(chip_id_t(queue_info.target_device));
        }
    }

    for (auto &graph_it : graphs) {
        tt_graph_info &graph_info = graph_it.second.my_graph_info;
        target_devices_set.insert(chip_id_t(graph_info.target_device));
    }

    return target_devices_set;
}

void netlist_workload_data::populate_queues_info_and_attributes() {
    parse_queue_producer_info();
    parse_queue_consumer_info();
    parse_dynamic_queue_info();
    parse_e2e_queue_info();
    parse_aliased_queue_info();
}

void netlist_workload_data::assert_constants_not_modified_in_programs() {
    for(const auto& program : programs) {
        const auto& program_trace = program.second.get_program_trace();
        std::vector<std::string> initialized_constants = {};
        std::vector<std::string> static_vars = {};
        uint32_t num_loops = 0;
        for(const auto& instruction : program_trace) {
            if(instruction.opcode == INSTRUCTION_OPCODE::Loop) {
                num_loops++;
            }
            else if(instruction.opcode == INSTRUCTION_OPCODE::EndLoop) {
                num_loops--;
            }
            else if(instruction.opcode ==  INSTRUCTION_OPCODE::Var || instruction.opcode == INSTRUCTION_OPCODE::StaticVar) {
                for(auto& var : instruction.vars) {
                    log_assert(std::find(initialized_constants.begin(), initialized_constants.end(), std::get<0>(var)) == initialized_constants.end(),
                                    "Constant {} in program {} is being overwritten by another instruction.", std::get<0>(var), program.first);
                    if(is_constant(std::get<0>(var))) {
                        log_assert(num_loops == 0, "Constant {} in program {} is being declared/initialized inside a loop.", std::get<0>(var), program.first);
                        if(!instruction.is_declaration) {
                            initialized_constants.push_back(std::get<0>(var));
                        }
                    }
                    if(instruction.opcode == INSTRUCTION_OPCODE::StaticVar) {
                        static_vars.push_back(std::get<0>(var));
                    }
                }
            }
            else if(instruction.opcode == INSTRUCTION_OPCODE::VarInst && is_constant(std::get<0>(instruction.vars[0]))) {
                log_assert(num_loops == 0, "Constant {} in program {} is being modified inside a loop.", std::get<0>(instruction.vars[0]), program.first);
                if(std::find(static_vars.begin(), static_vars.end(), std::get<0>(instruction.vars[0])) != static_vars.end()) {
                    log_assert(instruction.varinst_opcode == VAR_INSTRUCTION_OPCODE::Set, "Static constant {} in program {} is changing value due to a VarInst instruction.", std::get<0>(instruction.vars[0]), program.first);
                    
                    if(std::find(static_vars.begin(), static_vars.end(), std::get<0>(instruction.vars[1])) != static_vars.end()) {
                        log_assert(is_constant(std::get<0>(instruction.vars[1])), "Static constant {} in program {} is being modified by a static variable.", std::get<0>(instruction.vars[0]), program.first);
                    }

                }
                if(std::find(initialized_constants.begin(), initialized_constants.end(), std::get<0>(instruction.vars[0])) == initialized_constants.end()) {
                    initialized_constants.push_back(std::get<0>(instruction.vars[0]));
                }
            }
        }
    }
}

void netlist_workload_data::parse_queue_consumer_info() {
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

void netlist_workload_data::parse_e2e_queue_info() {
    // Go through all queues and add those that have a producer graph and a consumer graph
    for (auto &queue_it : queues) {
        tt_queue_info &queue_info = queue_it.second.my_queue_info;
        if (queue_info.type == IO_TYPE::Queue) {
            if (queues.at(queue_info.name).my_producer_info.graphs.size() > 0) {
                if (queues.at(queue_info.name).my_consumer_info.graphs.size() > 0) {
                    e2e_queues.insert(queue_info.name);
                }
            }
        }
    }
}

void netlist_workload_data::populate_op_to_outputs() {
    for (const auto &graph_it : graphs) {
        for (const auto &op_it : graph_it.second.my_graph_info.op_map) {
            for (const auto &input: op_it.second.input_names) {
                bool is_queue = queues.find(input) != queues.end();
                if (!is_queue) {
                    if (op_to_outputs.find(input) == op_to_outputs.end()) {
                        op_to_outputs.insert({input, {}});
                    }
                    op_to_outputs.at(input).insert(op_it.first);
                }
            }
        }
    }
    for (const auto &queue_it: queues) {
        string queue_name = queue_it.first;
        string queue_input = queue_it.second.my_queue_info.input;
        if (op_to_outputs.find(queue_input) == op_to_outputs.end()) {
            op_to_outputs.insert({queue_input, {}});
        }
        op_to_outputs.at(queue_input).insert(queue_name);
    }
}

void netlist_workload_data::parse_dynamic_queue_info() {
    // Go through all programs and all "allocate_queues" to derive static queues
    for (const auto &program : programs) {
        for (const auto &instruction : program.second.get_program_trace()) {
            if (instruction.opcode == INSTRUCTION_OPCODE::AllocateQueue) {
                for (const auto &var : instruction.vars) {
                    dynamic_queues.insert(get<0>(var));
                }
            }
        }
    }
}
void netlist_workload_data::parse_queue_producer_info() {
    std::unordered_map<std::string, std::vector<std::string>> op_to_qs_map;

    for (auto &queue_it : queues) {
        tt_queue_info &queue_info = queue_it.second.my_queue_info;
        if (!netlist_parser::is_queue_fed_by_op(queue_info)) {
            // host inputs are always in row-major scan, tilized format
            tt_queue_producer_info_t &prod_info = queue_it.second.my_producer_info;
            prod_info.is_tilized = true;
            prod_info.ublock_scan =
                queue_it.second.my_queue_info.dim
                    .ublock_order;  // Infer the ublock order for the producer based on that of the consumer
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

bool netlist_workload_data::is_e2e_queue(string queue_name) { return e2e_queues.find(queue_name) != e2e_queues.end(); }

bool netlist_workload_data::is_dynamic_queue(string queue_name) {
    return dynamic_queues.find(queue_name) != dynamic_queues.end();
}

bool netlist_workload_data::has_tilized_data(string queue_name) const {
    log_assert(
        queues.find(queue_name) != queues.end(),
        "netlist_workload_data::has_tilized_data queue_name={} Must be a valid queue name!",
        queue_name);
    return (queues.at(queue_name).my_producer_info.is_tilized);
}

Dim netlist_workload_data::get_ublock_scan(string queue_name) {
    log_assert(
        queues.find(queue_name) != queues.end(),
        "netlist_workload_data::get_ublock_scan queue_name={} Must be a valid queue name!",
        queue_name);
    return queues.at(queue_name).my_queue_info.dim.ublock_order;
}

// Write the temporal graph instance insturctions map for a given instance when finished parsing it from netlist.
void netlist_workload_data::write_map_for_temporal_graph_instance(
    std::string program_name,
    int temporal_graph_id,
    std::map<std::string, int> &graph_name_to_instruction_index_in_instance) {
    std::vector<int> execute_instructions_for_instance;

    // Create list of execute instruction indexes for graphs within the curent temporal epoch instance.
    for (const auto &kv : graph_name_to_instruction_index_in_instance) {
        auto instruction_index = kv.second;
        execute_instructions_for_instance.push_back(instruction_index);
    }

    // Store that same list for each instruction index in the current temporal epoch instance.
    for (const auto &kv : graph_name_to_instruction_index_in_instance) {
        auto instruction_index = kv.second;
        temporal_graph_instance_instructions[program_name][instruction_index] = execute_instructions_for_instance;
        log_debug(
            tt::LogNetlist,
            "Writing to map for program_name: {} instruction_index: {} the list execute_instructions_for_instance: {}",
            program_name,
            instruction_index,
            execute_instructions_for_instance);
    }

    // Clear storage for this instance now that it is finished.
    graph_name_to_instruction_index_in_instance.clear();
}

// Check to make sure all temporal epoch graphs are included when populating map of temporal graph instances to execute
// statements.
void netlist_workload_data::ensure_all_graphs_in_temporal_epoch_included(
    int temporal_epoch_id,
    std::map<std::string, int> &visited_graphs,
    std::map<int, std::set<std::string>> &reference_graphs) {
    // First make sure all visited graphs for this instance are in reference graphs for this temporal epoch. Then do the
    // reverse check.
    for (auto &g : visited_graphs) {
        log_assert(
            reference_graphs.at(temporal_epoch_id).find(g.first) != reference_graphs.at(temporal_epoch_id).end(),
            "Did not find graph_name: {} in reference graphs for temporal_epoch_id: {}",
            g.first,
            temporal_epoch_id);
    }

    for (auto &graph_name : reference_graphs.at(temporal_epoch_id)) {
        log_assert(
            visited_graphs.find(graph_name) != visited_graphs.end(),
            "Did not find graph_name: {} in visited graphs for temporal_epoch_id: {}",
            graph_name,
            temporal_epoch_id);
    }
}

void netlist_workload_data::parse_aliased_queue_info() {
    std::unordered_map<size_t, std::unordered_set<string>> io_allocation_hash = {};
    for (const std::pair<string, tt_queue_wrap> &queues_it : queues) {
        if ((queues_it.second.my_queue_info.type == IO_TYPE::RandomAccess) and
            is_static_queue(queues_it.first)) {  // Only support aliasing rams
            size_t hashed_result = 0;
            bool first = true;
            std::vector<tt_address_range> address_ranges =
                get_allocation_address_ranges_for_queue_info(queues_it.second.my_queue_info);
            for (const auto &address_range : address_ranges) {
                if (first) {
                    hashed_result = std::hash<tt_address_range>{}(address_range);
                    first = false;
                } else {
                    std::hash_combine(hashed_result, std::hash<tt_address_range>{}(address_range));
                }
            }
            if (io_allocation_hash.find(hashed_result) != io_allocation_hash.end()) {
                io_allocation_hash.at(hashed_result).insert(queues_it.first);
            } else {
                io_allocation_hash.insert({hashed_result, {queues_it.first}});
            }
        }
    }

    // Determine the smallest entry size or max entries then populate the io_to_base_io map
    for (const auto &io_allocation_hash_it : io_allocation_hash) {
        if (io_allocation_hash_it.second.size() <= 1) {
            continue;
        }
        int max_entries = INT_MIN;
        string max_entries_io = "";
        for (const auto &io_name : io_allocation_hash_it.second) {
            if (parser.queue_map.at(io_name).entries > max_entries) {
                max_entries = parser.queue_map.at(io_name).entries;
                max_entries_io = io_name;
            }
        }
        for (const auto &io_name : io_allocation_hash_it.second) {
            if (io_name != max_entries_io) {
                io_to_base_io.insert({io_name, max_entries_io});
                log_assert(
                    (max_entries % parser.queue_map.at(io_name).entries) == 0,
                    "IO={} is an alias of IO={}, but the entries={} must evenly divide entries={}",
                    io_name,
                    parser.queue_map.at(io_name).entries,
                    max_entries_io,
                    max_entries);
            }
        }
    }

    // allocate all the aliased IO
    for (const std::pair<string, tt_queue_wrap> &queues_it : queues) {
        if (io_to_base_io.find(queues_it.first) != io_to_base_io.end()) {
            queues[queues_it.first].my_io->base_io = queues[io_to_base_io.at(queues_it.first)].my_io;
        }
    }
}
bool netlist_workload_data::is_aliased_io(std::string lhs, std::string rhs) {
    if ((io_to_base_io.find(lhs) == io_to_base_io.end()) and (io_to_base_io.find(rhs) == io_to_base_io.end())) {
        return false;
    } else if ((io_to_base_io.find(lhs) != io_to_base_io.end()) and (io_to_base_io.find(rhs) != io_to_base_io.end())) {
        // both rhs and lhs has a base io --> compare the base io
        return (io_to_base_io.at(lhs) == io_to_base_io.at(rhs));
    } else if (io_to_base_io.find(lhs) != io_to_base_io.end()) {
        // lhs has a base io --> compare to rhs
        return (io_to_base_io.at(lhs) == rhs);
    } else if (io_to_base_io.find(rhs) != io_to_base_io.end()) {
        // rhs has a base io --> compare to lhs
        return (io_to_base_io.at(rhs) == lhs);
    }
    return false;
}

std::vector<tt_address_range> netlist_workload_data::get_allocation_address_ranges_for_queue_info(
    const tt_queue_info &queue_info) {
    std::vector<tt_address_range> address_ranges;
    for (const auto dram_allocation : queue_info.alloc_info) {
        int io_size = queue_info.entries * tt::size::get_entry_size_in_bytes(
                                               queue_info.data_format,
                                               has_tilized_data(queue_info.name),
                                               queue_info.dim.ublock_ct,
                                               queue_info.dim.ublock_rt,
                                               queue_info.dim.mblock_m,
                                               queue_info.dim.mblock_n,
                                               queue_info.dim.t,
                                               get_tile_dim_y(queue_info),
                                               get_tile_dim_x(queue_info));
        log_assert(
            io_size > 0,
            "queue={} must have valid allocation size, ublock_ct={} -- ublock_rt={} -- mblock_m={} --mblock_n={} -- "
            "t={} -- entries={}",
            queue_info.name,
            queue_info.dim.ublock_ct,
            queue_info.dim.ublock_rt,
            queue_info.dim.mblock_m,
            queue_info.dim.mblock_n,
            queue_info.dim.t,
            queue_info.entries);
        address_ranges.push_back(tt_address_range(
            {.device = static_cast<uint32_t>(queue_info.target_device),
             .channel = dram_allocation.channel,
             .base_byte_address = static_cast<uint64_t>(dram_allocation.address),
             .end_byte_address = static_cast<uint64_t>(dram_allocation.address + io_size - 1),
             .byte_size = static_cast<uint64_t>(io_size)}));
    }
    log_assert(
        address_ranges.size() > 0, "No dram allocation information found for queue={}", queue_info.name);
    return address_ranges;
}
std::vector<tt_address_range> netlist_workload_data::get_allocation_address_ranges_for_queue(const string &queue_name) {
    log_assert(
        this->queues.find(queue_name) != this->queues.end(),
        "Cannot find queue={} missing in netlist queues section",
        queue_name);
    return get_allocation_address_ranges_for_queue_info(this->queues.at(queue_name).my_queue_info);
}
void netlist_workload_data::populate_instruction_temporal_graph_mappings(const netlist_parser &parser) {
    for (const auto &program_name : this->program_order) {
        const auto &program_info = parser.program_map.at(program_name);
        int instruction_count = program_info.program_trace.size();

        // 1st pass - compute map of temporal epoch ids to graph names, and get filtered list of execute instructions
        // for simpler 2nd pass.
        std::map<int, std::set<std::string>> temporal_epoch_id_to_graph_names_map;
        std::vector<int> execute_instruction_indices;

        for (int i = 0; i < instruction_count; i++) {
            const auto &instruction = program_info.program_trace.at(i);
            if (instruction.opcode == INSTRUCTION_OPCODE::Execute) {
                execute_instruction_indices.push_back(i);
                const auto &graph_name = instruction.graph_name;
                const auto temporal_graph_id = parser.get_temporal_graph_of_graph(graph_name);
                if (temporal_epoch_id_to_graph_names_map[temporal_graph_id].find(graph_name) ==
                    temporal_epoch_id_to_graph_names_map[temporal_graph_id].end()) {
                    temporal_epoch_id_to_graph_names_map[temporal_graph_id].insert(graph_name);
                    log_debug(
                        tt::LogNetlist,
                        "temporal_epoch_id_to_graph_names_map : graph_name: {} added to temporal_graph_id: {}",
                        graph_name,
                        temporal_graph_id);
                }
            }
        }

        // 2nd pass - Determine which temporal_epoch instance each execute statement belongs to and create map of
        // execute statement to all execute statements for that instance.
        std::map<std::string, int> graph_name_to_instruction_index_in_instance;
        int prev_temporal_graph_id = -1;

        for (int j = 0; j < execute_instruction_indices.size(); j++) {
            const auto i = execute_instruction_indices.at(j);
            const auto &instruction = program_info.program_trace.at(i);

            log_assert(instruction.opcode == INSTRUCTION_OPCODE::Execute, "Expected Execute instruction");

            auto graph_name = instruction.graph_name;
            auto temporal_graph_id = parser.get_temporal_graph_of_graph(graph_name);
            bool is_new_instance = graph_name_to_instruction_index_in_instance.find(graph_name) !=
                                   graph_name_to_instruction_index_in_instance.end();
            bool is_new_temporal_epoch = prev_temporal_graph_id != -1 and prev_temporal_graph_id != temporal_graph_id;
            bool final_instruction = j == execute_instruction_indices.size() - 1;

            log_debug(
                tt::LogNetlist,
                "Parsing execute instruction at j: {} i: {} for graph_name: {} program_name: {} temporal_graph_id: {} "
                "is_new_temporal_epoch: {} is_new_instance: {} final_instruction: {}",
                j,
                i,
                graph_name,
                program_name,
                temporal_graph_id,
                is_new_temporal_epoch,
                is_new_instance,
                final_instruction);

            if (is_new_temporal_epoch or is_new_instance) {
                ensure_all_graphs_in_temporal_epoch_included(
                    prev_temporal_graph_id,
                    graph_name_to_instruction_index_in_instance,
                    temporal_epoch_id_to_graph_names_map);
                write_map_for_temporal_graph_instance(
                    program_name,
                    temporal_graph_id,
                    graph_name_to_instruction_index_in_instance);  // Write previous instance to map.
            }

            if (final_instruction) {
                graph_name_to_instruction_index_in_instance[graph_name] = i;
                ensure_all_graphs_in_temporal_epoch_included(
                    temporal_graph_id,
                    graph_name_to_instruction_index_in_instance,
                    temporal_epoch_id_to_graph_names_map);
                write_map_for_temporal_graph_instance(
                    program_name,
                    temporal_graph_id,
                    graph_name_to_instruction_index_in_instance);  // Write current instance to map.
            } else {
                graph_name_to_instruction_index_in_instance[graph_name] = i;
            }

            prev_temporal_graph_id = temporal_graph_id;
        }
    }
}
}  // namespace tt
