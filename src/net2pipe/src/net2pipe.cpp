// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "net2pipe.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "buda_soc_descriptor.h"
#include "buffer_info.h"
#include "netlist_fused_op_info_types.hpp"
#include "net2pipe_common.h"
#include "netlist_info_types.hpp"
#include "netlist_utils.hpp"
#include "common/tt_cluster_graph.hpp"
#include "router.hpp"
#include "router/router_passes_common.h"
#include "router_types.h"
#include "size_lib.hpp"
#include "src/net2pipe/inc/router_types.h"
#include "unique_id_generator.h"
#include "utils/logger.hpp"
#include "validators.h"
#include "buffer_type.h"
#include "utils/scoped_timer.hpp"
#include "device/cpuset_lib.hpp"

namespace {

  void emit_single_prolog_buffer(YAML::Emitter &out, const n2p::DeterministicKeyMap& deterministic_id_map, const n2p::prolog_buffer& p) {
    out << YAML::BeginMap;
    out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(p.uniqid)));
    out << YAML::Comment(p.comment);
    out << YAML::Value << YAML::BeginMap;

    out << SET_KEY_VAL("md_op_name", p.md_op_name);
    out << SET_KEY_VAL("buffer_type", n2p::c_PrologRelay);
    out << SET_KEY_VAL("id", p.id);
    out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(p.uniqid));
    out << SET_KEY_VAL("epoch_tiles", p.epoch_tiles);
    out << SET_KEY_VAL("chip_id", YAML::Flow << YAML::BeginSeq << p.chip_id << YAML::EndSeq);
    out << SET_KEY_VAL("core_coordinates", SET_COORD2(p.core_r, p.core_c));
    out << SET_KEY_VAL("size_tiles", p.size_tiles);
    //out << SET_KEY_VAL("scatter_gather_num_tiles", p.scatter_gather_num_tiles);
    out << SET_KEY_VAL("scatter_gather_num_tiles", p.size_tiles);
    out << SET_KEY_VAL("is_scatter", 0);
    out << SET_KEY_VAL("replicate", 0);
    out << SET_KEY_VAL("tile_size", p.tile_size);
    //out << SET_KEY_VAL("tile_clear_granularity", p.tile_clear_granularity);
    out << SET_KEY_VAL("dram_io_flag_is_remote", 0);
    out << SET_KEY_VAL("dram_buf_flag", 0);
    out << SET_KEY_VAL("dram_buf_streaming", 0);
    out << SET_KEY_VAL("write_dram_buf_flag", 0);
    out << SET_KEY_VAL("dram_chan", 0);
    out << SET_KEY_VAL("dram_sub_chan", 0);
    out << SET_KEY_VAL("dram_addr", 0);
    out << SET_KEY_VAL("q_slots", 0);
    out << SET_KEY_VAL("dram_io_flag", 0);
    out << SET_KEY_VAL("dram_io_allow_overwrite", 0);
    out << SET_KEY_VAL("dram_io_skip_flow_ctrl", 0);
    out << SET_KEY_VAL("dram_ram_flag", 0);
    out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
    out << SET_KEY_VAL("ublock_rt", 0);
    out << SET_KEY_VAL("ublock_ct", 0);
    out << SET_KEY_VAL("mblock_m", 0);
    out << SET_KEY_VAL("mblock_n", 0);
    out << SET_KEY_VAL("mblock_k", 0);
    out << SET_KEY_VAL("untilized_output", 0);
    out << SET_KEY_VAL("untilized_output_full_r_dim", 0);
    out << SET_KEY_VAL("untilized_output_full_c_dim", 0);
    out << SET_KEY_VAL("untilized_output_r_dim", 0);
    out << SET_KEY_VAL("untilized_output_c_dim", 0);
    out << SET_KEY_VAL("untilized_output_z_dim", 0);
    out << SET_KEY_VAL("untilized_output_type_0_zdim", 0);
    out << SET_KEY_VAL("untilized_output_type_1_zdim", 0);
    out << SET_KEY_VAL("is_post_tm_relay_buf", 1); // Mark relay buff so that pipegen can optimize out

    out << YAML::EndMap;
    out << YAML::EndMap;
  }

  template <typename Item>
  int num_unique_items( vector<Item> const& v ) {
    return set<Item>{ v.begin(), v.end() }.size();
  }; 


}
namespace n2p {
  prolog_layout get_prolog_layout(
    int consumer_t,
    int consumer_ublock_tiles_k, int consumer_mblock_ublocks_k,
    int consumer_ublock_tiles_c,
    int consumer_mblock_ublocks_n,
    int consumer_num_cores_r, int consumer_num_cores_c
  ) {
    prolog_layout result;

    result.num_cores_to_mcast   = consumer_num_cores_c;
    
    // Total number of tiles for one column of cores
    const int consumer_col_input_block_tiles = (consumer_ublock_tiles_k * consumer_mblock_ublocks_k) * (consumer_ublock_tiles_c * consumer_mblock_ublocks_n);
    const int consumer_total_tiles_per_col = consumer_t * consumer_col_input_block_tiles;

    // Currently only supports one chunk per core and will ensure that each chunk has the same number of tiles
    result.num_chunks_to_gather = consumer_num_cores_r;
    while (result.num_chunks_to_gather > 0 and consumer_total_tiles_per_col % result.num_chunks_to_gather != 0) {
        result.num_chunks_to_gather--;
    }
    assert(result.num_chunks_to_gather > 0);
    
    result.chunk_size_tiles = consumer_total_tiles_per_col / result.num_chunks_to_gather;

    result.num_cores_r = consumer_num_cores_r;
    result.num_cores_c = consumer_num_cores_c;

    return result;
  }
  int get_max_tiles_at_output(const consumer_to_producer_tile_map& input_tm) {
    int max_num_tiles = 0;
    for(auto & pipe_it : input_tm.pipes) {
        if (pipe_it.second.tile_map.size() > max_num_tiles) {
            max_num_tiles = pipe_it.second.tile_map.size();
        }
    }
    return max_num_tiles;
  }

  void unroll_pipes(consumer_to_producer_tile_map& input_tm) {
    std::map<int, phase_pipe_tile_map>   new_pipes;
    int pipe_index = 0;
    for (auto &pipe_it : input_tm.pipes) {
        auto &pipe = pipe_it.second;
        auto [core_offset_r, core_offset_c] = pipe.dest_cores.at(0);
        if(core_offset_r == -1) {
            for(int i = 0; i < input_tm.consumer_num_cores_r; i++) {
                new_pipes[pipe_index].tile_map = pipe.tile_map;
                new_pipes[pipe_index].dest_cores.push_back(std::make_pair(i, core_offset_c));
                new_pipes[pipe_index].dest_mcast = false;
                pipe_index++;
            }
        }
        else {
            assert(core_offset_c == -1);
            for(int i = 0; i < input_tm.consumer_num_cores_c; i++) {
                new_pipes[pipe_index].tile_map = pipe.tile_map;
                new_pipes[pipe_index].dest_cores.push_back(std::make_pair(core_offset_r, i));
                new_pipes[pipe_index].dest_mcast = false;
                pipe_index++;
            }
        }
    }
    input_tm.pipes = new_pipes;
  }


  };  // namespace n2p


Net2Pipe::Net2Pipe(
    const std::string &netlist_file,
    const std::string &output_dir,
    const std::string &epoch_start,
    const std::string &soc_descriptor_list_file_path,
    const std::string &cluster_description_file_path) {
    this->start_epoch_id = std::stoi(epoch_start);
    this->netlist_file = netlist_file;
    this->output_dir = output_dir;

    this->parsed_netlist.parse_file(netlist_file);

    for (auto queue_it : this->parsed_netlist.queue_map) {
        if (queue_it.second.loc != QUEUE_LOCATION::HOST){
            workload_target_device_ids.insert(queue_it.second.target_device);
        }
    }
    for (auto graph_it : this->parsed_netlist.graph_map) {
        workload_target_device_ids.insert(graph_it.second.target_device);
    }

    if (cluster_description_file_path.size() == 0) {
        // Physical MMIO device id's do not matter here in Net2pipe, just number of devices.
        this->cluster_description = ClusterDescription::create_for_grayskull_cluster(workload_target_device_ids, {});
    } else {
        this->cluster_description = ClusterDescription::create_from_yaml(cluster_description_file_path);
    }
    {
        // Initialize soc descriptors
        YAML::Node device_descriptor_yaml = YAML::LoadFile(soc_descriptor_list_file_path);
        std::unordered_map<std::string, chip_id_t> processed_soc_descriptors{}; // To avoid re-parsing the same descriptor.
        if (device_descriptor_yaml["chip_descriptors"].IsDefined()) {
            for (auto const& [chip_id, soc_descriptor_path] : device_descriptor_yaml["chip_descriptors"].as<std::map<int, std::string>>()) {
                if (processed_soc_descriptors.count(soc_descriptor_path) == 0) {
                    this->soc_descriptors[chip_id] = *load_soc_descriptor_from_yaml(soc_descriptor_path);
                    processed_soc_descriptors[soc_descriptor_path] = chip_id;
                } else {
                    this->soc_descriptors[chip_id] = this->soc_descriptors.at(processed_soc_descriptors.at(soc_descriptor_path));
                }
            }
        } else {
            for (chip_id_t chip_id : this->cluster_description->get_all_chips()) {
                this->soc_descriptors[chip_id] = *load_soc_descriptor_from_yaml(soc_descriptor_list_file_path);
            }
        }
    }
    {
        // Initial config
        this->config = {
            .is_feature_ethernet_multichip_compile_enabled=this->soc_descriptors.begin()->second.ethernet_cores.size() > 0,
            .arch=this->soc_descriptors.begin()->second.arch
        };
    }
    set_ring_start_end_device_ids();
}


std::uint64_t Net2Pipe::get_next_unique_id(std::vector<std::uint64_t>& per_epoch_hronological_unique_ids, int align, int id_block) const {
    auto new_key = this->unique_id_gen.get_next_unique_id(align, id_block);
    per_epoch_hronological_unique_ids.emplace_back(new_key);
    return new_key;
}

int Net2Pipe::get_next_epoch_id() {
    static int next_id = 0;
    return next_id++;
}


bool Net2Pipe::find_matching_graph_exec(GraphExecVars &graph_exec) {
    for (auto it = this->graph_exec_trace.begin(); it != graph_exec_trace.end(); ++it) {
        if (it->instrn.graph_name == graph_exec.instrn.graph_name) {
            bool graph_exec_arguments_match = true;
            for (uint32_t s = 0; s < graph_exec.queue_settings.size(); s++) {
                graph_exec_arguments_match &= (graph_exec.queue_settings[s].prolog == it->queue_settings[s].prolog);
                graph_exec_arguments_match &= (graph_exec.queue_settings[s].epilog == it->queue_settings[s].epilog);
            }
            // FIXME imatosevic - should the flags other than input_count be handled dynamically rather than uniquified?
            if (graph_exec_arguments_match) {
                return true;
            }
        }
    }
    return false;
}

bool all_graph_outputs_go_to_dram(const netlist_parser &netlist_parser, const std::string &graph_name) {
    const auto &graph_info = netlist_parser.graph_map.at(graph_name);
    std::unordered_set<std::string> unconsumed_ops = {};
    for (const auto &[op_name, op_info] : graph_info.op_map) {
        unconsumed_ops.insert(op_name);
    }

    for (const auto &[queue_name, queue_info] : netlist_parser.queue_map) {
        unconsumed_ops.erase(queue_info.input);
    }

    for (const auto &[op_name, op_info] : graph_info.op_map) {
        for (const auto &input_names : op_info.input_names) {
            unconsumed_ops.erase(input_names);
        }
    }

    return unconsumed_ops.size() == 0;
}

void Net2Pipe::run_instruction(netlist_program &program) {
    // Need to just store the variables and instruction properly for net2pipe.  Issue is that variables are now maps to
    // strings.... We can still save the list of the vars their names.

    program.run_instruction_with_execute_callback([&](netlist_program &program) {
        GraphExecVars graph_exec;
        tt_instruction_info instrn = program.get_current_instruction();
        graph_exec.instrn = instrn;
        for (auto exec_q_set : instrn.queue_settings) {
            QueueSettings q_set;
            q_set.name = exec_q_set.name;
            auto prolog_variable = program.get_variable(exec_q_set.prolog);
            auto epilog_variable = program.get_variable(exec_q_set.epilog);
            if ((prolog_variable.type == VARIABLE_TYPE::PARAM) or (epilog_variable.type == VARIABLE_TYPE::PARAM)) {
                ERROR(
                    "queue=" + exec_q_set.name +
                    " cannot have prologue or epilogue queue_settings which are runtime parameters controlled");
            }
            q_set.prolog = prolog_variable.value;
            q_set.epilog = epilog_variable.value;
            graph_exec.queue_settings.push_back(q_set);
        }
        if (!this->find_matching_graph_exec(graph_exec)) {
            this->graph_exec_uniquified.push_back(graph_exec);
            const auto &graph_name = graph_exec.instrn.graph_name;
            TT_ASSERT(graph_name.size() > 1, "Graph name is empty");
            int graph_temporal_epoch = parsed_netlist.get_temporal_graph_of_graph(graph_name);
            log_debug(
                tt::LogNet2Pipe,
                "temporal_epoch {}\t adding graph {}",
                graph_temporal_epoch,
                graph_exec.instrn.graph_name);
            this->temporal_epoch_graph_exec_vars[graph_temporal_epoch].push_back(graph_exec);
        }
        this->graph_exec_trace.push_back(graph_exec);
    });
}

void Net2Pipe::get_program_trace() {
    // Run programs sequentially
    for (string program_name : this->parsed_netlist.program_order) {
        TT_ASSERT(
            this->parsed_netlist.program_map.find(program_name) != this->parsed_netlist.program_map.end(),
            "Program being executed doesn't exist...");
        netlist_program program(program_name, this->parsed_netlist.program_map[program_name].program_trace);

        program.set_ignore_runtime_parameters(true);

        program.reset();
        while (!program.done()) {
            run_instruction(program);
        }
    }
}

void Net2Pipe::output_pipes() {
    validate::validate_netlist_fits_on_arch(this->parsed_netlist, this->soc_descriptors);

    if (this->config.is_feature_ethernet_multichip_compile_enabled) {
        validate::validate_netlist_fits_on_cluster(this->parsed_netlist, *(this->cluster_description.get()));
    }

    temporal_epoch_context dummy_context;

    // output queues in a separate file, make sure their IDs are globally unique
    for (auto it : this->parsed_netlist.queue_map) {
        tt_queue_info queue_info = it.second;
        std::string queue_name = it.first;
        log_debug(tt::LogRouter, "Creating queue_buffers for queue: {}", queue_name);
        std::uint64_t queue_unique_id = get_next_unique_id(dummy_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);  // not used in pipegen.yaml, just when we need unique ID per queue rather than per buffer
        dummy_context.queue_unique_id_info_map[queue_unique_id] = queue_info;
        dummy_context.queue_name_unique_id_map[queue_name] = queue_unique_id;
        for (int r = 0; r < queue_info.grid_size.r; r++) {
            for (int c = 0; c < queue_info.grid_size.c; c++) {
              int total_buf_size_tiles = queue_info.dim.ublock_rt * queue_info.dim.ublock_ct * queue_info.dim.mblock_m * queue_info.dim.mblock_n * queue_info.dim.t;
              this->queue_name_buf_unique_id_map[queue_name][r][c] =
                  get_next_unique_id(dummy_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN, total_buf_size_tiles);
                log_debug(
                    tt::LogRouter, "\tid: {}, r={}, c={}", this->queue_name_buf_unique_id_map[queue_name][r][c], r, c);
            }
        }
    }

    for (auto graph_it : this->parsed_netlist.graph_map) {
        tt_graph_info graph_info = graph_it.second;
        for (auto op_it : graph_info.op_map) {
            tt_op_info op_info = op_it.second;
            if (this->op_graph_map.count(op_info.name)) {
                ERROR(
                    "Op name " + op_info.name + " not unique, found in graphs " + graph_info.name + " and " +
                    this->op_graph_map[op_info.name].name);
            }
            this->op_graph_map[op_info.name] = graph_info;
            dummy_context.op_info_map[op_info.name] = op_info;
        }
    }

    get_program_trace();

    for (auto it = this->graph_exec_trace.begin(); it != this->graph_exec_trace.end(); ++it) {
        n2p::Log() << "Executed graph: " << it->instrn.graph_name << "\n";
    }

    n2p::Log() << "\n **** pipegen.yaml files output ****\n\n";

    // Build static dram padding tables for all epochs for all devices. Padding table is identical per dram
    this->build_padding_table();
    this->emit_padding_table();
    // For the time being, the router does not support routing across multiple chips
    // concurrently, hence the loops contains collect netlist info, route, and export
    // all in a single iteration. To support multichip routing for WH, we will nned
    // to split the loop into two, and run a single routing pass between them.
    // Additionally, router will need to be able to track resources per epoch.

    size_t num_temporal_epochs = this->temporal_epoch_graph_exec_vars.size();
    {
        std::vector<temporal_epoch_context> epoch_contexts(num_temporal_epochs);

        tt::parallel_for(
            0,
            num_temporal_epochs,
            [&](int temporal_epoch) {
                const auto &graph_exec_vars = this->temporal_epoch_graph_exec_vars.at(temporal_epoch);
                temporal_epoch_context& epoch_context = epoch_contexts[temporal_epoch];
                epoch_context.op_info_map = dummy_context.op_info_map;
                epoch_context.input_count =
                    this->parsed_netlist.graph_map.at(graph_exec_vars.at(0).instrn.graph_name).input_count;
                epoch_context.queue_unique_id_info_map = dummy_context.queue_unique_id_info_map;
                epoch_context.queue_name_unique_id_map = dummy_context.queue_name_unique_id_map;

                collect_epoch_info(graph_exec_vars, epoch_context);

                // route this epoch only
                this->run_router(temporal_epoch, epoch_context, *cluster_description);
            }, tt::cpuset::get_allowed_num_threads());

        n2p::DeterministicKeyMap deterministic_id_map;
        {
            // Iterate over all unique keys generate by this->unique_id_gen
            // and generate deterministic version for each key.
            n2p::UniqueIdGenerator id_gen;
            for (std::uint64_t key : dummy_context.horonological_unique_keys) {
                deterministic_id_map.insert(id_gen, key);
            }

            for (const auto &context : epoch_contexts) {
                for (std::uint64_t key : context.horonological_unique_keys) {
                    deterministic_id_map.insert(id_gen, key);
                }
            }
        }

        emit_queues_yaml(dummy_context, deterministic_id_map);

        // go for each, generate new id map
        // std::unordered_map<std::uint64_t, router::router_buffer_info_t> buffer_map; // router_buffer_info
        // stores routing coordinates std::unordered_map<std::uint64_t, pipe_t> pipes;
        // pass id map on export and use it for all unique ids.
        tt::parallel_for(
            0,
            num_temporal_epochs,
            [&](int temporal_epoch) {
                const auto &graph_exec_vars = this->temporal_epoch_graph_exec_vars.at(temporal_epoch);
                temporal_epoch_context &epoch_context = epoch_contexts[temporal_epoch];

                const int epoch_id = start_epoch_id + temporal_epoch;
                log_debug(
                    tt::LogNet2Pipe,
                    "Graph Temporal Epoch: {}, Assigned Epoch: {}",
                    temporal_epoch,
                    epoch_id);
                const auto &out_dir = this->create_temporal_epoch_output_directory(epoch_id);

                YAML::Emitter out_yaml;
                std::map<std::string, bool> op_queue_emitted;
                std::unordered_map<string, tt_op_info> temporal_epoch_op_map;
                for (const auto &graph_exec_var : graph_exec_vars) {
                    // Emit the final routed graph to pipegen yaml file for this epoch
                    n2p::Log() << "Uniquified graph: " << graph_exec_var.instrn.graph_name << "\n";
                    emit_epoch(graph_exec_var, epoch_context, deterministic_id_map, temporal_epoch, op_queue_emitted, out_yaml);
                    const tt_graph_info &graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
                    temporal_epoch_op_map.insert(graph_info.op_map.begin(), graph_info.op_map.end());
                }
                process_dram_fork_pipes(temporal_epoch_op_map, epoch_context);

                this->emit_padding_buffers(out_yaml, epoch_context, deterministic_id_map);
                this->emit_relay_buffers(
                    this->parsed_netlist.graph_map.at(graph_exec_vars.at(0).instrn.graph_name).input_count,
                    epoch_context,
                    deterministic_id_map,
                    out_yaml);
                this->emit_pipes(out_yaml, epoch_context, deterministic_id_map);
                // This pass has to be called after emit_pipes
                this->dump_queue_to_core_map_to_file(out_dir, false, epoch_context);
                this->dump_queue_to_core_map_to_file(out_dir, true, epoch_context);
                dump_yaml_to_file(out_yaml, out_dir);
                emit_operand_and_pipe_info(temporal_epoch_op_map, epoch_id, epoch_context, deterministic_id_map);
            }, tt::cpuset::get_allowed_num_threads());
    }
}


void Net2Pipe::check_op_resource_usage(const tt_graph_info &graph_info, temporal_epoch_context& epoch_context) const {

  // FIXME imatosevic - add checks for queue->op TM/reblock resources

  const int MAX_FORK_STREAMS_PER_CORE = 16;
  const int MAX_STREAM_PHASES_PER_CORE = 1800;
  const int MAX_DRAM_QUEUE_OP_PIPE_INPUTS = 2000;

  for (auto op_it : graph_info.op_map) {
    tt_op_info op_info = op_it.second;
    int num_op_inputs = op_info.input_names.size();
    int op_consumer_phases = 0;
    int op_producer_phases = 0;
    int op_producer_streams = 0;
    int op_dram_reads = 0;
    std::map <std::string, int> input_consumer_phases; 
    std::map <std::string, int> input_dram_reads; 
    std::map <std::string, int> output_producer_phases; 
    std::map <std::string, int> output_producer_streams; 
    for (int i = 0; i < num_op_inputs; i++) {
      const std::string &input_name = op_info.input_names[i];
      if (name_is_op(input_name, epoch_context)) {
        consumer_to_producer_tile_map tm = epoch_context.op_input_tm_pipes_map[op_info.name][input_name];
        int consumer_phases = tm.max_consumer_core_phases();
        op_consumer_phases += consumer_phases;
        input_consumer_phases[input_name] = consumer_phases;
      } else { // queue input
        consumer_to_producer_tile_map tm = epoch_context.op_input_tm_pipes_map[op_info.name][input_name];
        int input_tile_clear_granularity = this->get_op_kernel_input_tile_clear_granularity(op_info, i);
        input_dram_reads[input_name] = tm.max_dram_queue_input_indexes(input_tile_clear_granularity);
        op_dram_reads += input_dram_reads[input_name];
      }
    }
    for (auto output_name : epoch_context.op_queue_output_map[op_info.name]) {
      if (name_is_op(output_name, epoch_context)) {
        tt_op_info consumer_op_info = epoch_context.op_info_map.at(output_name);
        int num_consumer_inputs = consumer_op_info.input_names.size();
        int consumer_input_index = -1;
        for (int i = 0; i < num_consumer_inputs; i++) {
          if (consumer_op_info.input_names[i] == op_info.name) {
            consumer_input_index = i;
            break;
          }
        }
        if (consumer_input_index == -1) {
          ERROR("Can't find index of input " + op_info.name << " for op " + output_name);
        }
        int consumer_tile_clear_granularity = this->get_op_kernel_input_tile_clear_granularity(consumer_op_info, consumer_input_index);
        consumer_to_producer_tile_map tm = epoch_context.op_input_tm_pipes_map[output_name][op_info.name];
        int producer_phases = tm.max_producer_core_phases(consumer_tile_clear_granularity, (op_info.buf_size_mb == 1));
        int producer_streams = tm.max_producer_core_fan_out();
        output_producer_phases[output_name] = producer_phases;
        output_producer_streams[output_name] = producer_streams;
        op_producer_phases += producer_phases;
        op_producer_streams += producer_streams;
      }
    }    
  
    if (((op_consumer_phases + op_producer_phases) > MAX_STREAM_PHASES_PER_CORE) ||
        (op_producer_streams > MAX_FORK_STREAMS_PER_CORE)) {
      n2p::Log() << "\n\n *** WARNING: op " << op_info.name << " has high stream/blob resource usage:" << std::endl;
      n2p::Log() << "   phases in input-side blob: " << op_consumer_phases << std::endl;
      n2p::Log() << "   phases in output-side blob: " << op_producer_phases << std::endl;
      n2p::Log() << "   streams in output-side fork: " << op_producer_streams << std::endl;
      for (int i = 0; i < num_op_inputs; i++) {
        const std::string &input_name = op_info.input_names[i];
        if (name_is_op(input_name, epoch_context)) {
          n2p::Log() << "         " << input_name << " -> " << op_info.name << " consumer phases: " << input_consumer_phases[input_name] << std::endl;        
        }
      }
      for (auto output_name : epoch_context.op_queue_output_map[op_info.name]) {
        if (name_is_op(output_name, epoch_context)) {
          n2p::Log() << "         " << op_info.name << " -> " << output_name << " producer phases: " << output_producer_phases[output_name] << std::endl;        
          n2p::Log() << "         " << op_info.name << " -> " << output_name << " producer streams: " << output_producer_streams[output_name] << std::endl;                
        }
      }
      n2p::Log() << "\n\n";
    }
    if (op_dram_reads > MAX_DRAM_QUEUE_OP_PIPE_INPUTS) {
      n2p::Log() << "\n\n *** WARNING: op " << op_info.name << " has a hign number of DRAM read indexes = " << op_dram_reads << std::endl;
      for (int i = 0; i < num_op_inputs; i++) {
        const std::string &input_name = op_info.input_names[i];
        if (!name_is_op(input_name, epoch_context)) {
          n2p::Log() << "         " << input_name << " -> " << op_info.name << " DRAM read indexes: " << input_dram_reads[input_name] << std::endl;        
        }
      }      
      n2p::Log() << "\n\n";
    }
  }
}


void Net2Pipe::get_graph_names(std::vector<std::string> &names_vec) {
    for (auto it = this->graph_exec_uniquified.begin(); it != this->graph_exec_uniquified.end(); ++it)
        names_vec.emplace_back(it->instrn.graph_name);
}

bool Net2Pipe::is_name_hw_tilize(const std::string &name, const temporal_epoch_context &epoch_context) const {
    bool result = false;
    if (!name_is_op(name, epoch_context) &&
        epoch_context.op_queue_output_map.find(name) != epoch_context.op_queue_output_map.end() &&
        epoch_context.op_queue_output_map.at(name).size()) {
        std::string consumer_op_name = epoch_context.op_queue_output_map.at(name)[0];
        tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
        if (n2p::get_op_class(consumer_op_info) == OpClass::Tilizer) {
      int input_index = -1;
      for (int i = 0; i < (int)(consumer_op_info.input_names.size()); i++) {
        if (consumer_op_info.input_names[i] == name) {
          input_index = i;
          break;
        }
      }
      TT_ASSERT(input_index != -1);
      return (input_index == 0);
        }
    }
    return result;
}

bool Net2Pipe::is_name_embedding_table_queue(
    const std::string &name, const temporal_epoch_context &epoch_context) const {
  bool result = false;
  if (!name_is_op(name, epoch_context) &&
      epoch_context.op_queue_output_map.find(name) != epoch_context.op_queue_output_map.end() &&
      epoch_context.op_queue_output_map.at(name).size()) {
    std::string consumer_op_name = epoch_context.op_queue_output_map.at(name)[0];
    tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
    if (n2p::get_op_class(consumer_op_info) == OpClass::Embedding) {
      int input_index = -1;
      for (int i = 0; i < (int)(consumer_op_info.input_names.size()); i++) {
        if (consumer_op_info.input_names[i] == name) {
          input_index = i;
          break;
        }
      }
      TT_ASSERT(input_index != -1);
      return (input_index == 0);
    }
  }
  return result;
}

bool Net2Pipe::is_name_embedding_index_queue(
    const std::string &name, const temporal_epoch_context &epoch_context) const {
  bool result = false;
  if (!name_is_op(name, epoch_context) &&
      epoch_context.op_queue_output_map.find(name) != epoch_context.op_queue_output_map.end() &&
      epoch_context.op_queue_output_map.at(name).size()) {
    std::string consumer_op_name = epoch_context.op_queue_output_map.at(name)[0];
    tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
    if (n2p::get_op_class(consumer_op_info) == OpClass::Embedding) {
      int input_index = -1;
      for (int i = 0; i < (int)(consumer_op_info.input_names.size()); i++) {
        if (consumer_op_info.input_names[i] == name) {
          input_index = i;
          break;
        }
      }
      TT_ASSERT(input_index != -1);
      return (input_index == 1);
    }
  }
  return result;
}

void Net2Pipe::emit_queues_yaml(const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) {
    YAML::Emitter out_yaml;
    out_yaml << YAML::BeginMap;
    out_yaml << YAML::Key << ("graph_name");
    out_yaml << YAML::Value << "queues_graph";
    out_yaml << YAML::EndMap;
    for (auto it : this->parsed_netlist.queue_map) {
        tt_queue_info queue_info = it.second;
        std::string queue_name = it.first;

        int mblock_size_tiles = queue_info.dim.ublock_ct * queue_info.dim.ublock_rt * queue_info.dim.mblock_m * queue_info.dim.mblock_n;
        bool tiles_have_header;
        // FIXME imatosevic - this code needs updating for various queue settings
        if(!netlist_parser::is_queue_fed_by_op(queue_info)) {
          tiles_have_header = true;
        } else {
          tt_op_info input_op_info = epoch_context.op_info_map.at(queue_info.input);
          if (input_op_info.untilize_output || is_name_embedding_table_queue(queue_name, epoch_context) || is_name_hw_tilize(queue_name, epoch_context)) {
            tiles_have_header = false;
          }
          else {
            tiles_have_header = true;
          }
        }
        int tile_size_bytes = n2p::get_format_tile_size_bytes(queue_info.data_format, tiles_have_header, queue_info.tile_dim);
        int q_slots = queue_info.entries;
        for (int r = 0; r < queue_info.grid_size.r; r++) {
            for (int c = 0; c < queue_info.grid_size.c; c++) {
                int dram_chan = get_dram_buf_chan(queue_name, r, c);
                std::uint64_t dram_addr = get_dram_buf_addr(queue_name, r, c);
                std::uint64_t buf_unique_id = this->queue_name_buf_unique_id_map[queue_name][r][c];

                // Wormhole needs offset into host address for MMIO transactions
                if (queue_info.loc == QUEUE_LOCATION::HOST && (this->config.arch == tt::ARCH::WORMHOLE ||
                                                               this->config.arch == tt::ARCH::WORMHOLE_B0)) {
                    dram_addr += 0x800000000;
                }

                out_yaml << YAML::BeginMap;
                out_yaml << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(buf_unique_id)));
                out_yaml << YAML::Comment(
                    "Queue " + queue_name + ": r = " + std::to_string(r) + ", c = " + std::to_string(c));
                out_yaml << YAML::Value << YAML::BeginMap;

                out_yaml << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(buf_unique_id));
                out_yaml << SET_KEY_VAL("id", 0);
                out_yaml << SET_KEY_VAL("core_coordinates", SET_COORD2(r, c));
                out_yaml << SET_KEY_VAL("size_tiles", mblock_size_tiles);
                out_yaml << SET_KEY_VAL("tile_size", tile_size_bytes);
                out_yaml << SET_KEY_VAL("dram_chan", dram_chan);
                out_yaml << SET_KEY_VAL("dram_addr", uint_to_string(dram_addr));
                out_yaml << SET_KEY_VAL("q_slots", q_slots);
                out_yaml << SET_KEY_VAL("md_op_name", queue_name);

                out_yaml << YAML::EndMap;
                out_yaml << YAML::EndMap;
            }
        }
    }

    std::string cmd = "mkdir -p " + this->output_dir;
    const int status = system(cmd.c_str());
    n2p::Log() << "return value=" << status << std::endl;

    std::ofstream out_file;
    std::string queues_yaml_out_file_path = this->output_dir + "/netlist_queues.yaml";
    n2p::Log() << "Writing queue yaml file: " << queues_yaml_out_file_path << "\n";
    out_file.open(queues_yaml_out_file_path);
    out_file << out_yaml.c_str();
    out_file.close();
}

bool Net2Pipe::name_is_queue(std::string name) const { return this->parsed_netlist.queue_map.count(name); }

bool Net2Pipe::name_is_op(std::string name, const temporal_epoch_context& epoch_context) const { return epoch_context.op_info_map.count(name); }

void Net2Pipe::connect_outputs_to_inputs(const tt_graph_info &graph_info, int input_count, temporal_epoch_context& epoch_context) const {
    for (auto op_it : graph_info.op_map) {
        tt_op_info op_info = op_it.second;
        int num_op_inputs = op_info.input_names.size();
        n2p::Log() << "  Op: " << op_info.name << ", " << num_op_inputs << " inputs, input_count = " << input_count
                  << "\n";
        for (int i = 0; i < num_op_inputs; i++) {
            const std::string &input_name = op_info.input_names[i];
            if (name_is_queue(input_name) || name_is_op(input_name, epoch_context)) {
                epoch_context.op_queue_output_map[input_name].push_back(op_info.name);
                epoch_context.op_input_name_map[op_info.name][i] = input_name;
            } else {
                ERROR("Output graph: " + graph_info.name << ", op: " + op_info.name + ", unknown input: " + input_name);
            }
        }
        for (auto q_it : this->parsed_netlist.queue_map) {
            const std::string &q_name = q_it.first;
            tt_queue_info q_info = q_it.second;
            if (q_info.input == op_info.name) {
                epoch_context.op_queue_output_map[op_info.name].push_back(q_name);
            }
        }
        if (n2p::get_op_class(op_info) == OpClass::FusedOp) {
          epoch_context.fused_op_schedule_map[op_info.name] = netlist_utils::get_map_of_consumer_ops_per_input(op_info, this->parsed_netlist.fused_ops_op_map);
        }
    }
}

void Net2Pipe::compute_consumer_input_tile_mappings(const tt_graph_info &graph_info, temporal_epoch_context& epoch_context) const {
    for (auto op_it : graph_info.op_map) {
        tt_op_info op_info = op_it.second;
        compute_op_tms(op_info.name, graph_info.input_count, epoch_context);
    }
    for (auto q_it : this->parsed_netlist.queue_map) {
        tt_queue_info queue_info = q_it.second;
        compute_queue_tms(queue_info.name, graph_info.input_count, epoch_context);
    }

    // Report prologing optimizations
    log_debug(tt::LogNet2Pipe, "Prolog Report:");
    
    for (const auto& [op_name, op_info] : graph_info.op_map) {
        log_debug(tt::LogNet2Pipe, "Op '{}': type: {}", op_name, op_info.type);
        assert(op_info.name == op_name);
        const int num_op_inputs = op_info.input_names.size();
        for (int input_num = 0; input_num < num_op_inputs; input_num++) {
            const auto& input_name = op_info.input_names.at(input_num);
            if(is_name_prolog_queue(input_name, epoch_context)) {
                const bool has_two_step_prolog = epoch_context.prolog_tm_pipes_map.find(op_name) != epoch_context.prolog_tm_pipes_map.end() and
                                         epoch_context.prolog_tm_pipes_map.at(op_name).find(input_name) != epoch_context.prolog_tm_pipes_map.at(op_name).end();

                const bool post_tm_prolog = epoch_context.prolog_post_tm_operand.find(op_name) != epoch_context.prolog_post_tm_operand.end() and
                                         epoch_context.prolog_post_tm_operand.at(op_name).find(input_name) != epoch_context.prolog_post_tm_operand.at(op_name).end() and
                                         epoch_context.prolog_post_tm_operand.at(op_name).at(input_name);
                const auto kernel_in_buf_tiles = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                log_debug(tt::LogNet2Pipe, "\tInput {}: '{}' : Queue is prologed: Post-tm: {} : Prolog Intermediates: {} : Kernel input buffer size (tiles): {}", input_num, input_name, post_tm_prolog, has_two_step_prolog, kernel_in_buf_tiles);
            }
            else if(name_is_queue(input_name)) {
                log_debug(tt::LogNet2Pipe, "\tInput {}: '{}' : Queue is not listed as prologed", input_num, input_name);
            }
            else if(name_is_op(input_name, epoch_context)) {
                log_debug(tt::LogNet2Pipe, "\tInput {}: '{}' : is fed from another Op", input_num, input_name);
            }
            else {
                TT_ASSERT(false, "Input Name @ '{}' is invalid: '{}'", input_num, input_name);
            }
        }
    }
}

void Net2Pipe::emit_relay_buffers(int runtime_input_count, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map, YAML::Emitter &out) const {

    auto get_eth_connected_buffer_id_and_core = [this](bool is_eth_core_relay, router::unique_id_t relay_buffer_id, router::router_buffer_info_t const& relay_buffer, const temporal_epoch_context& epoch_context) -> std::optional<std::tuple<router::unique_id_t, tt_cxy_pair, bool>> {
        // Return nullopt if this relay buffer is not connected to another over ethernet
        // else return {connected_buffer_id, connected_buffer_core, connected_buffer_is_sender_core}
        router::unique_id_t first_producer_buffer = epoch_context.pipes.at(epoch_context.buffer_input_pipes.at(relay_buffer_id)).input_buffer_ids.at(0);
        tt_cxy_pair const &first_producer_buffer_core = epoch_context.buffer_map.at(first_producer_buffer).core_location();
        bool is_eth_link_receiver_relay =
            is_eth_core_relay && first_producer_buffer_core.chip != relay_buffer.chip_location();
        
        if (is_eth_link_receiver_relay) {
            return std::tuple<router::unique_id_t, tt_cxy_pair, bool>(first_producer_buffer, first_producer_buffer_core, true);
        }

        router::unique_id_t first_consumer_buffer = epoch_context.pipes.at(*(epoch_context.buffer_output_pipes.at(relay_buffer_id).begin())).output_buffer_ids().at(0);
        tt_cxy_pair const &first_consumer_buffer_core = epoch_context.buffer_map.at(first_consumer_buffer).core_location();
        bool is_eth_link_sender_relay =
            is_eth_core_relay && first_consumer_buffer_core.chip != relay_buffer.chip_location();
        if (is_eth_link_sender_relay) {
            return std::tuple<router::unique_id_t, tt_cxy_pair, bool>(first_consumer_buffer, first_consumer_buffer_core, true);
        }
        
        return std::nullopt;
    };

    std::unordered_map<tt_cxy_pair, int> used_hardware_ethernet_streams = {};
    std::unordered_set<router::unique_id_t> hardware_ethernet_stream_buffers = {};
    std::unordered_set<router::unique_id_t> firmware_ethernet_stream_buffers = {};
    for (const auto &[buf_id, buffer] : epoch_context.buffer_map) {
        bool is_relay_buffer = !buffer.is_queue() && buffer.info().type() == tt::RouterBufferType::Relay;
        if (is_relay_buffer) {
            // output is always in the form of a single buffer entry in pipegen.yaml
            // that may be implicitly multiplied for scatter buffers

            // Is scatter just multiple output pipes or is it also target location dependent?
            const auto &output_pipes = epoch_context.buffer_output_pipes.at(buf_id);
            bool output_pipes_scatter = std::any_of(std::begin(output_pipes), std::end(output_pipes), [&](auto pipe_id) { return epoch_context.pipes.at(pipe_id).is_scatter() || epoch_context.pipes.at(pipe_id).has_multiple_timesteps(); });
            int routing_r = buffer.core_location().y;
            int routing_c = buffer.core_location().x;
            chip_id_t buffer_chip = buffer.chip_location();
            auto potential_eth_core_iter = std::find(
                this->soc_descriptors.at(buffer_chip).ethernet_cores.begin(),
                this->soc_descriptors.at(buffer_chip).ethernet_cores.end(),
                tt_xy_pair(routing_c, routing_r));
            bool is_eth_core_relay = potential_eth_core_iter != this->soc_descriptors.at(buffer_chip).ethernet_cores.end();

            log_trace(tt::LogNet2Pipe, "Relay Buffer: {} (c={},y={},c={})", buf_id, buffer.chip_location(), routing_r, routing_c);
            auto const& eth_link_connected_buffer_and_core = get_eth_connected_buffer_id_and_core(is_eth_core_relay, buf_id, buffer, epoch_context);
            bool uses_eth_link = eth_link_connected_buffer_and_core != std::nullopt;
            bool requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams = false;
            if (uses_eth_link) {
                auto const& [eth_connected_buf_id, eth_connected_core, connected_is_sender] = eth_link_connected_buffer_and_core.value();
                bool already_mapped_to_hardware_ethernet_stream = hardware_ethernet_stream_buffers.find(eth_connected_buf_id) != hardware_ethernet_stream_buffers.end();
                bool already_mapped_to_firmware_ethernet_stream = firmware_ethernet_stream_buffers.find(eth_connected_buf_id) != firmware_ethernet_stream_buffers.end();
                bool already_mapped = already_mapped_to_hardware_ethernet_stream || already_mapped_to_firmware_ethernet_stream;
                TT_ASSERT(!(already_mapped_to_hardware_ethernet_stream && already_mapped_to_firmware_ethernet_stream), "Buffer {} was mapped to both hardware and firmware ethernet stream. It can be mapped to only one");
                if (already_mapped) {
                    requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams =
                        requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams ||
                        already_mapped_to_firmware_ethernet_stream;

                } else {
                    bool both_ends_have_hardware_ethernet_streams_available =
                        used_hardware_ethernet_streams[eth_connected_core] < MAX_NUM_ETH_HARDWARE_STREAMS_TOTAL &&
                        used_hardware_ethernet_streams[buffer.core_location()] < MAX_NUM_ETH_HARDWARE_STREAMS_TOTAL;
                    requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams =
                        !both_ends_have_hardware_ethernet_streams_available;

                    if (both_ends_have_hardware_ethernet_streams_available) {
                        used_hardware_ethernet_streams[buffer.core_location()]++;
                        hardware_ethernet_stream_buffers.insert(buf_id);
                        used_hardware_ethernet_streams[eth_connected_core]++;
                        hardware_ethernet_stream_buffers.insert(eth_connected_buf_id);
                    }

                }
                log_trace(tt::LogNet2Pipe, "\tUses eth link. Total links on core = {}. Requires FW managed stream ? {}", used_hardware_ethernet_streams[buffer.core_location()], (requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams? "Y":"N"));
            }
            if (env_var("TT_FORCE_USE_FW_ETH_STREAMS", false)) {
                requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams = true;
            }
            if (env_var("TT_ENABLE_FW_ETH_STREAMS", 0) == 0) {
                requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams = false;
            }
            bool override_to_use_fw_ethernet_stream =
                uses_eth_link && requires_ethernet_link_stream_spillover_into_firmware_managed_ethernet_streams; // &&
                // std::get<2>(eth_link_connected_buffer_and_core.value());

            int r = is_eth_core_relay ? routing_r : this->soc_descriptors.at(buffer_chip).routing_y_to_worker_y.at(routing_r);
            int c = is_eth_core_relay ? routing_c : this->soc_descriptors.at(buffer_chip).routing_x_to_worker_x.at(routing_c);
            int output_replicate = 0;
            bool output_is_scatter = false;
            output_is_scatter = std::any_of(std::begin(output_pipes), std::end(output_pipes), [&](auto pipe_id) {
                return epoch_context.pipes.at(pipe_id).is_scatter();
            });
            int output_scatter_gather_num_tiles = buffer.info().scatter_gather_num_tiles();

            if (override_to_use_fw_ethernet_stream) {
                // output_is_scatter = true;
                // output_replicate = 1;//output_buf.info().replication_factor();
            }

            TT_ASSERT(!output_pipes_scatter, "Relay buffers can't feed scatter pipes");
            int dram_input_noc_id = 0; // FIXME imatosevic - does this ever need to be set for relay buffers? 
            int stream_id = buffer.info().stream_id();
            TT_ASSERT(stream_id >= 0, "Invalid stream id of ", stream_id, " for buffer ", buf_id);
            int output_size_tiles = buffer.info().allocated_size_in_tiles();
            int output_tile_size_bytes = buffer.info().tile_size_in_bytes();
            TT_ASSERT(buffer.info().tile_dims().x == 32 && buffer.info().tile_dims().y == 32);
            int output_epoch_tiles = buffer.info().total_epoch_tiles();

            out << YAML::BeginMap;
            out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(buf_id)));
            out << YAML::Comment("Relay Buffer: [r=" + std::to_string(r) + ", c=" + std::to_string(c) + "]");
            out << YAML::Value << YAML::BeginMap;

            out << SET_KEY_VAL("md_op_name", std::to_string(deterministic_id_map.get_deterministic_key(buf_id)));
            out << SET_KEY_VAL("buffer_type", (is_eth_core_relay ? n2p::c_EthernetRelay : n2p::c_Relay));
            out << SET_KEY_VAL("id", stream_id);
            out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(buf_id));
            out << SET_KEY_VAL("epoch_tiles", output_epoch_tiles);
            out << SET_KEY_VAL("chip_id", YAML::Flow << YAML::BeginSeq << buffer.core_location().chip << YAML::EndSeq);
            out << SET_KEY_VAL("tiles_per_input", output_epoch_tiles/runtime_input_count);
            if (is_eth_core_relay) {
                size_t ethernet_chan =
                    std::distance(this->soc_descriptors.at(buffer.chip_location()).ethernet_cores.begin(), potential_eth_core_iter);
                TT_ASSERT(ethernet_chan < this->soc_descriptors.at(buffer.chip_location()).ethernet_cores.size(), "Invalid ethernet channel ", ethernet_chan, " doesn't exist for arch");
                out << SET_KEY_VAL("ethernet_chan", ethernet_chan);
            }
            out << SET_KEY_VAL("core_coordinates", SET_COORD2(r, c));
            out << SET_KEY_VAL("size_tiles", output_size_tiles);
            out << SET_KEY_VAL("scatter_gather_num_tiles", output_scatter_gather_num_tiles);
            out << SET_KEY_VAL("is_scatter", static_cast<int>(output_is_scatter));
            out << SET_KEY_VAL("replicate", output_replicate);
            out << SET_KEY_VAL("tile_size", output_tile_size_bytes);
            out << SET_KEY_VAL("dram_io_flag_is_remote", 0);
            out << SET_KEY_VAL("dram_buf_flag", 0);
            out << SET_KEY_VAL("dram_buf_streaming", 0);
            out << SET_KEY_VAL("write_dram_buf_flag", 0);
            out << SET_KEY_VAL("dram_prefetch_incoming_noc_id", dram_input_noc_id);  /// ****
            out << SET_KEY_VAL("dram_chan", 0);
            out << SET_KEY_VAL("dram_sub_chan", 0);
            out << SET_KEY_VAL("dram_addr", 0);
            out << SET_KEY_VAL("q_slots", 0);
            out << SET_KEY_VAL("dram_io_flag", 0);
            out << SET_KEY_VAL("dram_io_allow_overwrite", 0);
            out << SET_KEY_VAL("dram_io_skip_flow_ctrl", 0);
            out << SET_KEY_VAL("dram_ram_flag", 0);
            out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
            // if (output_buf_shared_with_int) {
            //   out << SET_KEY_VAL("buffer_space_shared", int_unique_id);
            // }
            out << SET_KEY_VAL("ublock_rt", buffer.info().ublock_rt());
            out << SET_KEY_VAL("ublock_ct", buffer.info().ublock_ct());
            out << SET_KEY_VAL("mblock_m", buffer.info().mblock_m());
            out << SET_KEY_VAL("mblock_n", buffer.info().mblock_n());
            if (override_to_use_fw_ethernet_stream) {
                out << SET_KEY_VAL("use_ethernet_fw_stream", 1);
            }
            // out << SET_KEY_VAL("mblock_k",  buffer.info().attributes.m_k);  //// ?

            // Relevant for relay buffers - probably need to worry about it?
            // emit_untilize_output(out, &op_info);

            out << YAML::EndMap;
            out << YAML::EndMap;
        }
    }
}

void Net2Pipe::emit_buffers(
    const tt_graph_info &graph_info,
    const temporal_epoch_context &epoch_context,
    const n2p::DeterministicKeyMap& deterministic_id_map,
    int input_count,
    std::map<std::string, bool> &op_queue_emitted,
    YAML::Emitter &out_yaml) const {;

    for (auto op_it : graph_info.op_map) {
        tt_op_info op_info = op_it.second;
        int num_op_inputs = op_info.input_names.size();
        this->emit_kernel_bufs(out_yaml, epoch_context, deterministic_id_map, op_info.name, input_count);
        for (const auto &input_name : op_info.input_names) {
            if (name_is_queue(input_name)) {
                if (!op_queue_emitted[input_name]) {
                    this->emit_queue(
                        out_yaml,
                        input_name,
                        graph_info.name,
                        epoch_context,
                        deterministic_id_map,
                        graph_info.target_device,
                        input_count,
                        epoch_context.queue_setting_map.at(input_name));
                    op_queue_emitted[input_name] = true;
                }
            } else if (!name_is_op(input_name, epoch_context)) {
                ERROR("Output graph: " + graph_info.name << ", op: " + op_info.name + ", unknown input: " + input_name);
            }
        }
        for (const auto & output_name : epoch_context.op_queue_output_map.at(op_info.name)) {
            if (name_is_queue(output_name)) {
                if (!op_queue_emitted[output_name]) {
                    this->emit_queue(
                        out_yaml,
                        output_name,
                        graph_info.name,
                        epoch_context,
                        deterministic_id_map,
                        graph_info.target_device,
                        input_count,
                        epoch_context.queue_setting_map.at(output_name));
                    op_queue_emitted[output_name] = true;
                }
            }
        }
    }
}

bool Net2Pipe::is_name_prolog_queue(const std::string& name, const temporal_epoch_context& epoch_context) const {
    return name_is_queue(name) and
        epoch_context.queue_setting_map.find(name) != epoch_context.queue_setting_map.end() and
        epoch_context.queue_setting_map.at(name).prolog;
}

void Net2Pipe::collect_epoch_buffer_info(const tt_graph_info &graph_info, int input_count, temporal_epoch_context& epoch_context) const {
    for (auto op_it : graph_info.op_map) {
        tt_op_info op_info = op_it.second;
        int num_op_inputs = op_info.input_names.size();
        if (netlist_utils::is_valid_ethernet_op(op_info.type)) {
            this->naive_place_unplaced_ethernet_datacopy_ops(op_info.name, epoch_context);
        }
        this->collect_kernel_buf_info(op_info.name, input_count, epoch_context);
        for (int i = 0; i < num_op_inputs; i++) {
            const std::string &input_name = op_info.input_names[i];
            if (name_is_queue(input_name)) {
                n2p::Log() << "     Queue input: " << input_name << "\n";
                this->read_epoch_queue_info(input_name, input_count, epoch_context.queue_setting_map.at(input_name), epoch_context);
                if (op_info.input_data_formats[i] != this->parsed_netlist.queue_map.at(input_name).data_format) {
                    ERROR(
                        "Output graph: " + graph_info.name
                        << ", op: " + op_info.name + ", input: " + input_name + " has incompatible data format");
                }
                if (epoch_context.queue_setting_map.at(input_name).epilog) {
                    ERROR(
                        "Output graph: " + graph_info.name
                        << ", op: " + op_info.name + ", input: " + input_name
                        << " -> input queue of an op must have epilogue=false");
                }
            } else if (name_is_op(input_name, epoch_context)) {
                n2p::Log() << "     Op input: " << input_name << "\n";
                if (op_info.input_data_formats[i] != epoch_context.op_info_map.at(input_name).output_data_format) {
                    ERROR(
                        "Output graph: " + graph_info.name
                        << ", op: " + op_info.name + ", input: " + input_name + " has incompatible data format");
                }
            } else {
                ERROR("Output graph: " + graph_info.name << ", op: " + op_info.name + ", unknown input: " + input_name);
            }
        }

        for (auto it : epoch_context.op_queue_output_map.at(op_info.name)) {
            if (name_is_queue(it)) {
                const std::string &output_q_name = it;
                const tt_queue_info& output_q_info = this->parsed_netlist.queue_map.at(output_q_name);
                if (op_info.output_data_format != this->parsed_netlist.queue_map.at(output_q_name).data_format) {
                    ERROR(
                        "Output graph: " + graph_info.name
                        << ", op: " + op_info.name + ", output: " + output_q_name + " has incompatible data format");
                }
                if (op_info.gradient_op) {
                    if (!epoch_context.queue_setting_map.at(output_q_name).prolog || !epoch_context.queue_setting_map.at(output_q_name).epilog) {
                        ERROR(
                            "Output graph: " + graph_info.name
                            << ", op: " + op_info.name + ", output: " + output_q_name
                            << " -> output queue of a gradient op must have prologue=true and epilogue=true");
                    }
                    if (output_q_info.type != IO_TYPE::RandomAccess) {
                        ERROR(
                            "Output graph: " + graph_info.name
                            << ", op: " + op_info.name + ", output: " + output_q_name
                            << " -> output queue of a gradient op must have ram type");
                    }
                } else {
                    if (epoch_context.queue_setting_map.at(output_q_name).prolog || epoch_context.queue_setting_map.at(output_q_name).epilog) {
                        ERROR(
                            "Output graph: " + graph_info.name
                            << ", op: " + op_info.name + ", output: " + output_q_name
                            << " -> output queue of a non-gradient op must have prologue=false and epilogue=false");
                    }
                }
                this->read_epoch_queue_info(output_q_name, input_count, epoch_context.queue_setting_map.at(output_q_name), epoch_context);
            }
        }
    }
}

void Net2Pipe::collect_pipe_info(const tt_graph_info &graph_info, temporal_epoch_context& epoch_context) const {
    for (const auto &[op_name, op_info] : graph_info.op_map) {
        collect_op_input_pipes(op_info.name, graph_info.input_count, epoch_context);
        for (auto it : epoch_context.op_queue_output_map[op_info.name]) {
            if (name_is_queue(it)) {
                const std::string &output_q_name = it;
                collect_queue_input_pipes(output_q_name, epoch_context);
            }
        }
    }

    for (auto &[pipe_id, pipe] : epoch_context.pipes) {
        if (pipe.is_scatter()) {
            auto const first_segment_location = pipe.scatter_segment_core_location(0);
            bool all_locations_same = std::all_of(
                pipe.core_locations().begin(), pipe.core_locations().end(), [&first_segment_location](auto const &loc) {
                    return loc == first_segment_location;
                });
            bool has_stack_padding = std::any_of(
                pipe.output_padding_buffer_list.begin(), pipe.output_padding_buffer_list.end(), [](auto const &buf_id) {
                    return buf_id != 0;
                });
            if (!all_locations_same or has_stack_padding) {
                continue;
            }

            pipe_output_list_t first_segment_outputs = pipe.time_multiplexed_output_buffer_ids().at(0);
            bool all_segments_same = std::all_of(
                pipe.time_multiplexed_output_buffer_ids().begin(),
                pipe.time_multiplexed_output_buffer_ids().end(),
                [&first_segment_outputs](auto const& outputs) {
                return outputs == first_segment_outputs; });
            if (all_segments_same) {
                pipe.locations.clear();
                pipe.locations.push_back(first_segment_location);
                pipe.output_buffer_ids_container = first_segment_outputs;
            }
        }
    }
}

void Net2Pipe::collect_epoch_info( const std::vector<GraphExecVars> &graph_exec_vars, temporal_epoch_context& epoch_context) const {

    for (const auto &graph_exec_var : graph_exec_vars) {
        // collect info for this epoch
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        log_debug(tt::LogNet2Pipe, "\tUniquified graph: {}", graph_exec_var.instrn.graph_name);
    }
    for (const auto &graph_exec_var : graph_exec_vars) {
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        int input_count = graph_info.input_count;
        this->connect_outputs_to_inputs(graph_info, input_count, epoch_context);
    }
    // Populate queue_setting_map for this entire epoch
    for (const auto &graph_exec_var : graph_exec_vars) {
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        for (const auto& [op_name, op_info] : graph_info.op_map) {
            for (const auto &input_name : op_info.input_names) {
                if (name_is_queue(input_name)) {
                    epoch_context.queue_setting_map[input_name] = {
                        .name = input_name,
                        .prolog = false,
                        .epilog = false,
                    };
                }
            }
            for (const auto & output_name : epoch_context.op_queue_output_map.at(op_name)) {
                if (name_is_queue(output_name)) {
                    epoch_context.queue_setting_map[output_name] = {
                        .name = output_name,
                        .prolog = false,
                        .epilog = false,
                    };
                }
            }
        }
    }
    for (const auto &graph_exec_var : graph_exec_vars) {
        for (const auto& q_it : graph_exec_var.queue_settings) {
            epoch_context.queue_setting_map[q_it.name] = q_it;
        }
    }
    for (const auto &graph_exec_var : graph_exec_vars) {
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        this->compute_consumer_input_tile_mappings(graph_info, epoch_context);
    }
    for (const auto &graph_exec_var : graph_exec_vars) {
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        this->check_op_resource_usage(graph_info, epoch_context);
        // FIXME imatosevic - extend this to op->queue reblocking resources
    }
    for (const auto &graph_exec_var : graph_exec_vars) {
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        int input_count = graph_info.input_count;
        this->collect_epoch_buffer_info(graph_info, input_count, epoch_context);
    }
    for (const auto &graph_exec_var : graph_exec_vars) {
        tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
        this->collect_pipe_info(graph_info, epoch_context);
    }
}

std::string Net2Pipe::create_temporal_epoch_output_directory(int temporal_epoch) const {
    std::stringstream yaml_output_dir;
    yaml_output_dir << this->output_dir
                    << "/temporal_epoch_" + std::to_string(temporal_epoch) +
                           "/overlay/";  // + "/graph_" << graph_info.name << "/overlay/";
    n2p::Log() << "  Directory: " << yaml_output_dir.str() << "\n";
    std::string cmd = "mkdir -p " + yaml_output_dir.str();
    const int status = system(cmd.c_str());
    n2p::Log() << "return value=" << status << std::endl;

    return yaml_output_dir.str();
}

void Net2Pipe::dump_yaml_to_file(YAML::Emitter const &out_yaml, const std::string &out_file_dir) const {
    std::ofstream out_file;
    out_file.open(out_file_dir + "/pipegen.yaml");
    out_file << out_yaml.c_str();
    out_file.close();
}

void Net2Pipe::emit_epoch(
    const GraphExecVars &graph_exec,
    const temporal_epoch_context &epoch_context,
    const n2p::DeterministicKeyMap& deterministic_id_map,
    int temporal_epoch,
    std::map<std::string, bool> &op_queue_emitted,
    YAML::Emitter &out_yaml) const {
    tt_graph_info graph_info = this->parsed_netlist.graph_map.at(graph_exec.instrn.graph_name);
    int input_count = graph_info.input_count;
    tt_instruction_info instrn_info = graph_exec.instrn;
    n2p::Log() << "\nOutput graph: " << graph_info.name << ", input count = " << input_count << "\n";

    out_yaml << YAML::BeginMap;
    out_yaml << YAML::Key << ("graph_name");
    out_yaml << YAML::Value << graph_info.name;
    out_yaml << YAML::EndMap;

    this->emit_buffers(graph_info, epoch_context, deterministic_id_map, input_count, op_queue_emitted, out_yaml);
}

std::uint64_t Net2Pipe::get_dram_buf_addr(std::string queue_name, int r, int c) const {
    tt_queue_info queue_info = this->parsed_netlist.queue_map.at(queue_name);
    tt_queue_allocation_info alloc_info = queue_info.alloc_info[r * queue_info.grid_size.c + c];
    return alloc_info.address;
}

int Net2Pipe::get_dram_buf_chan(std::string queue_name, int r, int c) const {
    tt_queue_info queue_info = this->parsed_netlist.queue_map.at(queue_name);
    tt_queue_allocation_info alloc_info = queue_info.alloc_info.at(r * queue_info.grid_size.c + c);
    return alloc_info.channel;
}


bool Net2Pipe::is_input_adjacent_mcast(std::string producer_name, std::string consumer_name, int& adjacent_noc_id, const temporal_epoch_context& epoch_context) const {

    if (!name_is_op(producer_name, epoch_context) || !name_is_op(consumer_name, epoch_context)) {
        return false;
    }
    tt_op_info producer_op_info = epoch_context.op_info_map.at(producer_name);
    tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_name);
    if (netlist_utils::is_valid_ethernet_op(producer_op_info.type) || netlist_utils::is_valid_ethernet_op(consumer_op_info.type)) {
        return false;
    }
    
    chip_id_t producer_chip = op_graph_map.at(producer_name).target_device;
    if (producer_chip != op_graph_map.at(consumer_name).target_device) {
        return false;
    }

    int input_index = -1;
    for (auto it : epoch_context.op_input_name_map.at(consumer_name)) {
        if (it.second == producer_name) {
            input_index = it.first;
            break;
        }
    }
    TT_ASSERT(input_index != -1);

    bool consumer_input_row_mcast;
    bool consumer_input_col_mcast;
    bool consumer_input_noc1_mcast;
    n2p::get_op_input_mcast(consumer_op_info, input_index, epoch_context.fused_op_schedule_map, consumer_input_row_mcast, consumer_input_col_mcast, consumer_input_noc1_mcast);

    bool result = n2p::producer_size_and_placement_for_direct_mcast(this->config.arch,
                                                                    producer_op_info, consumer_op_info,
                                                                    consumer_input_row_mcast, consumer_input_col_mcast,
                                                                    consumer_input_noc1_mcast,
                                                                    this->soc_descriptors.at(producer_chip).worker_grid_size.x, this->soc_descriptors.at(producer_chip).worker_grid_size.y,
                                                                    adjacent_noc_id);

    return result;
}

bool n2p::producer_size_and_placement_for_direct_mcast(
                tt::ARCH device_arch,
                const tt_op_info& producer_op_info, const tt_op_info& consumer_op_info, 
                bool consumer_input_row_mcast, bool consumer_input_col_mcast,
                bool consumer_input_noc1_mcast,
                int worker_grid_size_x, int worker_grid_size_y,
                int& adjacent_noc_id) {

    // FIXME imatosevic - feature doesn't work on GS due to code size issues 
    // (due to RISC bug with branching between code memories)
    // revise this later as we evaluate the feature impact
    if (device_arch != tt::ARCH::WORMHOLE_B0) {
        return false;
    }

    consumer_input_row_mcast = consumer_input_row_mcast && (consumer_op_info.grid_size_logical_c() > 1);
    consumer_input_col_mcast = consumer_input_col_mcast && (consumer_op_info.grid_size_logical_r() > 1);

    bool consumer_input_x_mcast = consumer_op_info.grid_transpose ? consumer_input_col_mcast : consumer_input_row_mcast;
    bool consumer_input_y_mcast = consumer_op_info.grid_transpose ? consumer_input_row_mcast : consumer_input_col_mcast;

    bool producer_noc0_x_adjacent = (consumer_op_info.grid_loc_x() == ((producer_op_info.grid_loc_x() + 1) % worker_grid_size_x));
    bool producer_noc1_x_adjacent = (producer_op_info.grid_loc_x() == ((consumer_op_info.grid_loc_x() + consumer_op_info.grid_size_x()) % worker_grid_size_x));

    bool producer_noc0_y_adjacent = (consumer_op_info.grid_loc_y() == ((producer_op_info.grid_loc_y() + 1) % worker_grid_size_y));
    bool producer_noc1_y_adjacent = (producer_op_info.grid_loc_y() == ((consumer_op_info.grid_loc_y() + consumer_op_info.grid_size_y()) % worker_grid_size_y));

    bool direct_x_mcast = 
        consumer_input_x_mcast && 
        (consumer_op_info.grid_size_y() == producer_op_info.grid_size_y()) &&
        (producer_op_info.grid_size_x() == 1) &&
        (consumer_op_info.grid_loc_y() == producer_op_info.grid_loc_y()) &&
        (producer_noc0_x_adjacent || producer_noc1_x_adjacent);

    bool direct_y_mcast = 
        consumer_input_y_mcast && 
        (consumer_op_info.grid_size_x() == producer_op_info.grid_size_x()) &&
        (producer_op_info.grid_size_y() == 1) &&
        (consumer_op_info.grid_loc_x() == producer_op_info.grid_loc_x()) &&
        (producer_noc0_y_adjacent || producer_noc1_y_adjacent);

    adjacent_noc_id = (direct_x_mcast && producer_noc1_x_adjacent) || (direct_y_mcast && producer_noc1_y_adjacent) ? 1 : 0;
    bool mcast_noc_match = consumer_input_noc1_mcast ? (adjacent_noc_id == 1) : (adjacent_noc_id == 0);
    return (direct_x_mcast || direct_y_mcast) && mcast_noc_match;
}

bool Net2Pipe::is_output_scatter(std::string producer_name, int &scatter_granularity, const temporal_epoch_context& epoch_context) const {
    bool result = false;
    int producer_scatter_granularity;
    if (name_is_op(producer_name, epoch_context)) {
        int op_output_single_buf_size_mblocks =
          n2p::get_op_output_single_buf_size_mblocks(epoch_context.op_info_map.at(producer_name));  // does not include double-buffering
        int mblock_tiles = n2p::get_op_output_mblock_tiles(epoch_context.op_info_map.at(producer_name));
        scatter_granularity = mblock_tiles * (n2p::get_op_class(epoch_context.op_info_map.at(producer_name)) == OpClass::Buffer ? 1 : op_output_single_buf_size_mblocks); // for buffer op scatter_granularity is always mblock_tiles (strip)
        producer_scatter_granularity = scatter_granularity;
    } else {
        const tt_queue_info &queue_info = this->parsed_netlist.queue_map.at(producer_name);
        if (is_name_embedding_table_queue(producer_name, epoch_context)) {
          scatter_granularity = 1;
        } else if (is_name_hw_tilize(producer_name, epoch_context)) {
          scatter_granularity = 1;
          if (queue_info.dim.mblock_n == 1) {
            int max_scatter_size_bytes = SEQUENTIAL_DRAM_IO_SCATTER_THRESHOLD;
            int tile_size_bytes = n2p::get_format_tile_size_bytes(queue_info.data_format, false, queue_info.tile_dim) * queue_info.dim.ublock_ct / tt::tile_dim_to_array(queue_info.tile_dim)[0];
            if (tt::tile_dim_to_array(queue_info.tile_dim)[0] * tile_size_bytes <= max_scatter_size_bytes) {
              scatter_granularity = tt::tile_dim_to_array(queue_info.tile_dim)[0];
            } 
          }
        } else {
          int mblock_tiles = get_mblock_size_tiles(queue_info);
          scatter_granularity = mblock_tiles * queue_info.dim.t;
        }
        producer_scatter_granularity = scatter_granularity;
    }

    if (epoch_context.op_queue_output_map.find(producer_name) != epoch_context.op_queue_output_map.end()) {
        for (std::string consumer_name : epoch_context.op_queue_output_map.at(producer_name)) {
            consumer_to_producer_tile_map tm;
            if (name_is_op(consumer_name, epoch_context) ) { // and not is_name_prolog_queue(producer_name, this->queue_setting_map)) {
                tt_op_info output_op_info = epoch_context.op_info_map.at(consumer_name);
                tm = epoch_context.op_input_tm_pipes_map.at(consumer_name).at(producer_name);
            } else {
                TT_ASSERT(!name_is_queue(producer_name), "queue " + producer_name + " has queue output " + consumer_name);
                tt_op_info producer_op_info = epoch_context.op_info_map.at(producer_name);
                tt_queue_info output_queue_info = this->parsed_netlist.queue_map.at(consumer_name);
                if (producer_op_info.untilize_output || producer_op_info.gradient_op) {
                    // no reblock/TM supported in these cases, so no ned to check for output scatter granularities
                    continue;
                } else {
                    tm = epoch_context.queue_input_tm_pipes_map.at(consumer_name);
                }
            }
            log_trace(tt::LogNet2Pipe, "is_output_scatter: producer_name: '{}', consumer_name: '{}', producer_scatter_granularity: {}, tm.scatter_granularit: {}",
                            producer_name, consumer_name, producer_scatter_granularity, tm.scatter_granularity);
            if (tm.producer_tiles_out_of_order) {
                result = true;
            }
            if (tm.scatter_granularity != producer_scatter_granularity) {
                result = true;
                if (!is_name_embedding_table_queue(producer_name, epoch_context) && !is_name_hw_tilize(producer_name, epoch_context) && ((producer_scatter_granularity % tm.scatter_granularity) != 0)) {
                    // for embedding queue tables we force scatter granularity to 1
                    ERROR(
                        std::string("Op: ") << consumer_name << ", input: " << producer_name
                                            << " - non-divisible TM scatter granularity = " << tm.scatter_granularity);
                }
                if (!is_name_hw_tilize(producer_name, epoch_context) && (tm.scatter_granularity % scatter_granularity) != 0) {
                    scatter_granularity = std::gcd(scatter_granularity, tm.scatter_granularity);
                }
            }
            // queue scatter_gather_num_tiles can't be larger than the destination op's input buffer
            if (name_is_queue(producer_name)) {
                tt_op_info output_op_info = epoch_context.op_info_map.at(consumer_name);
                bool found = false;
                int op_num_inputs = epoch_context.op_input_name_map.at(consumer_name).size();
                for (int op_input_index = 0; op_input_index < op_num_inputs; op_input_index++) {
                    const string op_input_name = epoch_context.op_input_name_map.at(consumer_name).at(op_input_index);
                    if (op_input_name == producer_name) {
                        found = true;
                        const bool is_two_step_prolog = epoch_context.prolog_layout_per_op.find(consumer_name) != epoch_context.prolog_layout_per_op.end() and
                                                        epoch_context.prolog_layout_per_op.at(consumer_name).find(op_input_name) != epoch_context.prolog_layout_per_op.at(consumer_name).end();
                        
                        const int op_input_buf_size_tiles = get_op_kernel_input_size_tiles(output_op_info, op_input_index, epoch_context);

                        log_trace(tt::LogNet2Pipe, "is_two_step_prolog: '{}': consumer op: '{}', input name: '{}', scatter_granularity: {}, input_buf_size_tiles: {}", 
                            is_two_step_prolog, consumer_name, op_input_name, scatter_granularity, op_input_buf_size_tiles);
                        
                        if (not is_two_step_prolog and op_input_buf_size_tiles < scatter_granularity) {
                            scatter_granularity = std::gcd(op_input_buf_size_tiles, scatter_granularity);
                        }

                        if (is_two_step_prolog) {
                            const int chunk_size_tiles = epoch_context.prolog_layout_per_op.at(consumer_name).at(op_input_name).chunk_size_tiles;
                            scatter_granularity = std::gcd(chunk_size_tiles, scatter_granularity);
                        }

                        log_trace(tt::LogNet2Pipe, "final scatter_granularity: {}", scatter_granularity);
                    }
                }
                if (!found) {
                    ERROR("Queue " << producer_name << ": can't trace back input from output op " << consumer_name);
                }
            }
        }
    
        if (!name_is_op(producer_name, epoch_context) && epoch_context.op_queue_output_map.at(producer_name).size() > 0) {
            result = true; // All dram reads are scatter buffers
            const tt_queue_info &queue_info = this->parsed_netlist.queue_map.at(producer_name);
            int tile_size_bytes = n2p::get_format_tile_size_bytes(queue_info.data_format, !is_name_embedding_table_queue(producer_name, epoch_context) && !is_name_hw_tilize(producer_name, epoch_context), queue_info.tile_dim);
            int max_size_tiles = SEQUENTIAL_DRAM_IO_SCATTER_THRESHOLD / tile_size_bytes;
            if (max_size_tiles < 1) {
                ERROR("Queue " << producer_name << ": cannot find scatter_granularity with given tile_size.");
            }
            if (scatter_granularity > max_size_tiles) {
                while (true) {
                    if (scatter_granularity % max_size_tiles == 0) {
                        scatter_granularity = max_size_tiles;
                        break;
                    }
                    max_size_tiles--;
                }
            }
        }
    }
    log_trace(tt::LogNet2Pipe, "is_output_scatter: result: '{}', producer_name: '{}', scatter_granularity: {}", result, producer_name, scatter_granularity);
    return result;
}

void Net2Pipe::read_epoch_queue_info(
    const std::string &queue_name, int input_count, const QueueSettings& queue_setting, temporal_epoch_context& epoch_context) const {
  
    if (!epoch_context.queue_name_unique_id_map.count(queue_name)) {
        ERROR("Queue " << queue_name << " not assigned unique ID");
    }

    if (epoch_context.op_queue_output_buf_granularity.count(queue_name)) {
      // queue forks to multiple outputs, already traversed in current epoch
      return;
    }

    const tt_queue_info &queue_info = this->parsed_netlist.queue_map.at(queue_name);
    int mblock_size_tiles = get_mblock_size_tiles(queue_info);

    int scatter_gather_num_tiles;
    const bool is_scatter = is_output_scatter(queue_name, scatter_gather_num_tiles, epoch_context);
    if (!is_scatter) {
        scatter_gather_num_tiles = mblock_size_tiles * queue_info.dim.t;
    }
    epoch_context.op_queue_output_scatter[queue_name] = is_scatter;
    epoch_context.op_queue_output_buf_granularity[queue_name] = scatter_gather_num_tiles;

    std::uint64_t queue_unique_id = epoch_context.queue_name_unique_id_map.at(queue_name);
    
    if (is_name_embedding_table_queue(queue_name, epoch_context) || is_name_hw_tilize(queue_name, epoch_context) || is_name_embedding_index_queue(queue_name, epoch_context)) {
      if (queue_setting.prolog || queue_setting.epilog) {
        ERROR("Queue " + queue_name + ": can't have prolog/epilog settings for embedding table and index queues");        
      }
    }

    int prolog_replicate = 1;
    if (queue_setting.prolog) {
      if (epoch_context.op_queue_output_map[queue_name].size()) {
        //log_debug(tt::LogNet2Pipe, "this->op_queue_output_map[queue_name]: queue_name: {}: {}", queue_name, fmt::join(this->op_queue_output_map[queue_name], ", "));
        prolog_replicate = num_unique_items(epoch_context.op_queue_output_map[queue_name]);
        for (std::string connected_op_name : epoch_context.op_queue_output_map[queue_name]) {
          n2p::Log() << " Queue " << queue_name << " is input for op " << connected_op_name << ", prolog=" << queue_setting.prolog << "\n";
          int op_target_device = this->op_graph_map.at(connected_op_name).target_device;
          if (op_target_device != queue_info.target_device) {
            ERROR("Queue " + queue_name + " has prolog=1 and is input for op " + connected_op_name + ", which is on a different target_device");
          } 
        }
      }
      else if (!epoch_context.op_info_map.count(queue_info.input)) {
        ERROR("Queue " + queue_name + " has prolog=1 and no input or output ops");
      }
    }

    for (int conn_op_index = 0; conn_op_index < prolog_replicate; conn_op_index++) { // outer loop is no-op unless it's a prolog with forked output
      for (int r = 0; r < queue_info.grid_size.r; r++) {
        for (int c = 0; c < queue_info.grid_size.c; c++) {
          int buffer_index = r * queue_info.grid_size.c + c;
          auto allocation_info = router::router_queue_allocation_info{queue_info.alloc_info.at(buffer_index), .is_host_mapped=queue_info.loc == QUEUE_LOCATION::HOST};
          auto queue_location = tt_cxy_pair(queue_info.target_device, 255, 255);
          int read_port = queue_info.alloc_info.at(buffer_index).read_port;
          int write_port = queue_info.alloc_info.at(buffer_index).write_port;
          std::uint64_t queue_buffer_id = this->queue_name_buf_unique_id_map.at(queue_name).at(r).at(c);
          if (queue_setting.prolog) {
            if (conn_op_index > 0) {
              // for prolog queues forking to multiple ops; allocate as many unique IDs as needed for the highest fan-out of fork in any epoch
              int total_buf_size_tiles = queue_info.dim.ublock_rt * queue_info.dim.ublock_ct * queue_info.dim.mblock_m * queue_info.dim.mblock_n * queue_info.dim.t;
              queue_buffer_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN, total_buf_size_tiles);
            }
            epoch_context.prolog_queue_name_fork_buf_unique_id_map[queue_name][r][c][conn_op_index] = queue_buffer_id;
            epoch_context.prologue_buf_unique_id_to_queue_info[queue_buffer_id] = {queue_name, tt_xy_pair(r, c), conn_op_index};
            log_debug(tt::LogNet2Pipe, "adding prolog buffer id '{}'; scatter_gather_num_tiles: {}", queue_buffer_id, scatter_gather_num_tiles);
          }

          // queue location has default (255, 255) assigment, this is properly assigned in router pass
          const router::router_buffer_info_t &queue_buffer_info = queue_info.loc == QUEUE_LOCATION::DRAM ?
            router::router_buffer_info_t::create_immutable(queue_location.chip, router::buffer_queue_info_t(queue_unique_id, allocation_info, queue_setting.prolog), read_port, write_port) :
            router::router_buffer_info_t::create_immutable(queue_location, router::buffer_queue_info_t(queue_unique_id, allocation_info, queue_setting.prolog), read_port, write_port);

          log_debug(
                        tt::LogRouter,
                        "Adding queue buffer id {} which belongs to queue {} with id {}",
                        queue_buffer_id,
                        queue_info.name,
                        queue_unique_id);

          epoch_context.buffer_map.insert({queue_buffer_id, queue_buffer_info});
          epoch_context.buffer_op_or_queue_name_map.insert({queue_buffer_id, queue_name});

          if (is_scatter) {
            int q_slot_size_tiles;
            int tiles_per_input;
            int tile_size_bytes;
            int buf_epoch_tiles;
            int replicate;
            bool is_scatter;
            this->get_queue_attributes(queue_info, input_count, queue_setting.prolog, epoch_context, q_slot_size_tiles, tiles_per_input, tile_size_bytes,
                                       buf_epoch_tiles, replicate, is_scatter);
            if (replicate * scatter_gather_num_tiles > n2p::UNIQUE_ID_ALIGN) {
                ERROR("Queue " + queue_name + " is too large to be representable with distinct unique ID. Its scatter offsets will spill into the next unique ID range.");
            }
            for (int rep = 1; rep < replicate; rep++) {
              epoch_context.buffer_map.insert({queue_buffer_id + rep*scatter_gather_num_tiles, queue_buffer_info});
              epoch_context.buffer_op_or_queue_name_map.insert({queue_buffer_id + rep*scatter_gather_num_tiles, queue_name});
            }
          }
        }
      }
    } 
}

int Net2Pipe::get_queue_dram_subchannel(std::uint64_t q_buf_id, const temporal_epoch_context& epoch_context) const {
    const auto &dram_core = epoch_context.buffer_map.at(q_buf_id).core_location();
    int subchannel = std::get<1>(this->soc_descriptors.at(dram_core.chip).dram_core_channel_map.at(dram_core));
    return subchannel;
}


// Determine if target_device is upstream or downstream from current epoch_device. Used to determine noc address. Jumps are not allowed.
bool Net2Pipe::is_target_device_downstream(int starting_device_id, int ending_device_id, int epoch_device, int target_device) const{

    TT_ASSERT(starting_device_id >= 0); // Make sure only chips that use PCIE P2P end up here.
    TT_ASSERT(ending_device_id >= 0);

    int downstream_peer_device_id = epoch_device < ending_device_id ? epoch_device + 1 : starting_device_id; // Increment or wrap to 0.
    int upstream_peer_device_id = epoch_device > starting_device_id ? epoch_device - 1 : ending_device_id;   // Decrement or wrap to max

    log_debug(
        tt::LogNet2Pipe,
        "is_target_device_downstream() - epoch_device: {} target_device: {} starting_device_id: {} ending_device: {} downstream_peer_device_id: {} upstream_peer_device_id: {}",
        epoch_device, target_device, starting_device_id, ending_device_id, downstream_peer_device_id, upstream_peer_device_id);

    bool is_downstream = false;

    if (target_device == downstream_peer_device_id){
        is_downstream = true;
    }else if (target_device == upstream_peer_device_id){
        is_downstream = false;
    }else{
        log_fatal("queue_info.target_device: {} is not an immediate neighbor (upstream or downstream) to epoch_device: {}",target_device, epoch_device);
    }

    return is_downstream;
}


void Net2Pipe::get_queue_attributes(const tt_queue_info &queue_info,
                                    int input_count,
                                    bool prolog,
                                    const temporal_epoch_context& epoch_context,
                                    int& q_slot_size_tiles,
                                    int& tiles_per_input,
                                    int& tile_size_bytes,
                                    int& buf_epoch_tiles, 
                                    int& replicate,
                                    bool& is_scatter) const {

  bool untilize_input = false;
  if (netlist_parser::is_queue_fed_by_op(queue_info)) {
    tt_op_info input_op_info = epoch_context.op_info_map.at(queue_info.input);
    untilize_input = input_op_info.untilize_output;
  }
  
  if (is_name_embedding_table_queue(queue_info.name, epoch_context)) {
    std::string consumer_op_name = epoch_context.op_queue_output_map.at(queue_info.name).at(0);
    tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
    q_slot_size_tiles = tiles_per_input = queue_info.dim.mblock_n * consumer_op_info.attributes.num_indices;
    // we set tile_size to the number of bytes we fetch for each input from a single core
    tile_size_bytes = n2p::get_format_tile_size_bytes(queue_info.data_format, false, queue_info.tile_dim) * queue_info.dim.ublock_ct / tt::tile_dim_to_array(queue_info.tile_dim)[0];
  } else if (is_name_hw_tilize(queue_info.name, epoch_context)) {
    q_slot_size_tiles = tiles_per_input = queue_info.dim.t * queue_info.dim.mblock_m * queue_info.dim.mblock_n * queue_info.dim.ublock_rt * tt::tile_dim_to_array(queue_info.tile_dim)[0];
    // we set tile_size to the number of bytes we fetch for each input from a single core
    tile_size_bytes = n2p::get_format_tile_size_bytes(queue_info.data_format, false, queue_info.tile_dim) * queue_info.dim.ublock_ct / tt::tile_dim_to_array(queue_info.tile_dim)[0];
  } else {
    int mblock_size_tiles = get_mblock_size_tiles(queue_info);
    q_slot_size_tiles = tiles_per_input = queue_info.dim.t * mblock_size_tiles;
    bool tiles_have_header = !untilize_input;
    tile_size_bytes = n2p::get_format_tile_size_bytes(queue_info.data_format, tiles_have_header, queue_info.tile_dim);
  }
  buf_epoch_tiles = q_slot_size_tiles * input_count;
  TT_ASSERT(buf_epoch_tiles > 0);
  replicate = 0;
  is_scatter = epoch_context.op_queue_output_scatter.at(queue_info.name);
  if (is_scatter) {
    replicate = q_slot_size_tiles / epoch_context.op_queue_output_buf_granularity.at(queue_info.name);
    buf_epoch_tiles /= replicate;
    TT_ASSERT(buf_epoch_tiles > 0);
    if (prolog) {
      q_slot_size_tiles /= replicate;
    } else {
      // q_slot_size_tiles should remain undivided because pipegen expects so
    }
  }
}


void Net2Pipe::emit_queue(
    YAML::Emitter &out,
    std::string queue_name,
    std::string graph_name,
    const temporal_epoch_context &epoch_context,
    const n2p::DeterministicKeyMap& deterministic_id_map,
    int epoch_device,
    int input_count,
    const QueueSettings& queue_settings
) const {
    const tt_queue_info &queue_info = this->parsed_netlist.queue_map.at(queue_name);
    bool external_input = !netlist_parser::is_queue_fed_by_op(queue_info);
    tt_op_info input_op_info;
    bool untilize_input = false;
    if (!external_input) {
        input_op_info = epoch_context.op_info_map.at(queue_info.input);
        untilize_input = input_op_info.untilize_output;
        if (input_op_info.gradient_op && (this->op_graph_map.at(queue_info.input).name == graph_name)) {
            // For gradient ops, we don't generate a separate DRAM buffer in pipegen.yaml.
            // Rather, the intermediate buffer is tagged with DRAM flags and address.
            return;
        }
    }

    int q_slot_size_tiles;
    int tiles_per_input;
    int tile_size_bytes;
    int buf_epoch_tiles;
    int replicate;
    bool is_scatter;
    this->get_queue_attributes(queue_info, input_count, queue_settings.prolog, epoch_context, q_slot_size_tiles, tiles_per_input, tile_size_bytes,
                               buf_epoch_tiles, replicate, is_scatter);


    // FIXME imatosevic - support for "zero" flag - todo
    // n2p::Log() << " *** " << op_name << ", " << queue_name << " -> " << queue_info.target_device << " -- " << epoch_device
    // <<  "\n";
    bool ethernet_chip_to_chip_supported = this->soc_descriptors.at(epoch_device).ethernet_cores.size() > 0;
    int queue_is_remote = (queue_info.loc == QUEUE_LOCATION::HOST) || ((not ethernet_chip_to_chip_supported) && (queue_info.target_device != epoch_device));
    int dram_buf_flag = queue_settings.prolog;
    int write_dram_buf_flag = queue_settings.epilog;

    const std::string c_buffer_type = [&queue_settings]() {
        if (queue_settings.prolog && queue_settings.epilog) {
            return n2p::c_Unknown;
        } else if (queue_settings.prolog) {
            return n2p::c_Prolog;
        } else if (queue_settings.epilog) {
            return n2p::c_Epilog;
        } else {
            return n2p::c_DramIO;
        }
    }();

    int q_slots = queue_info.entries;

    int dram_io_flag = !queue_settings.prolog && !queue_settings.epilog;
    int dram_ram_flag = (queue_info.type == IO_TYPE::RandomAccess);
    int dram_io_skip_flow_ctrl = (queue_info.type == IO_TYPE::RandomAccess);
    int dram_io_allow_overwrite = (queue_info.type == IO_TYPE::RandomAccess);
    
    int prolog_replicate = 1;
    if (queue_settings.prolog && epoch_context.op_queue_output_map.at(queue_name).size()) {
        prolog_replicate = num_unique_items(epoch_context.op_queue_output_map.at(queue_name));
    }

    for (int conn_op_index = 0; conn_op_index < prolog_replicate; conn_op_index++) {  // outer loop is no-op unless it's a prolog with forked output

      for (int r = 0; r < queue_info.grid_size.r; r++) {
        
        for (int c = 0; c < queue_info.grid_size.c; c++) {
          
          int dram_chan = get_dram_buf_chan(queue_name, r, c);
          std::uint64_t dram_addr = get_dram_buf_addr(queue_name, r, c);

          // FIXME imatosevic - this will change when we move to direct p2p pipes
          if (queue_info.loc == QUEUE_LOCATION::HOST) {
            // For system memory queues, address specifies offset into the 256MB section.
            // Outgoing TLB allocated to host is the one with the local device ID
            // (otherwise unused since there is no loopback).

            // upper bits 31,30 peer_id == 0 for HOST. Ensure we are not exceeding 1GB host address, otherwise we need to explicitly clear these bits.
            TT_ASSERT(dram_addr == (dram_addr % PEER_REGION_SIZE), "dram_addr for Host Queue " + queue_info.name + " exceeds PEER_REGION_SIZE = " + std::to_string(PEER_REGION_SIZE));

            // Non-GS arch supports 4 memory channels, GS only supports ch0.
            TT_ASSERT(this->config.arch != tt::ARCH::GRAYSKULL || dram_chan == 0, "Encountered loc:HOST queue in Grayskull with memory ch > 0: not supported.");
            dram_addr += (dram_chan * PEER_REGION_SIZE);

            // Wormhole needs offset into host address for MMIO transactions
            if (this->config.arch == tt::ARCH::WORMHOLE ||
                this->config.arch == tt::ARCH::WORMHOLE_B0) {
              dram_addr += 0x800000000;
            }
          } else if (!ethernet_chip_to_chip_supported && queue_info.target_device != epoch_device) {

            bool is_downstream = is_target_device_downstream(starting_device_id, ending_device_id, epoch_device, queue_info.target_device);

            // Keep only 256MB addr bits, set the peer_id bit based on us/ds device (1GB), and add 192 MB offset to reach DRAM TLBs in 512MB PCIE BAR.
            dram_addr = (PEER_REGION_SIZE * (is_downstream ? 2 : 1)) +
            (dram_addr % DRAM_REGION_SIZE) +
            PCIE_BAR_OFFSET_REMOTE_QUEUE_DRAM;

            // n2p::Log() << "Remote Device setup. starting_device_id: " << starting_device_id
            // << " ending_device_id: " << ending_device_id << " epoch_device: " << epoch_device
            // << " target_device: " << queue_info.target_device << " is_downstream: " << is_downstream
            // << " dram_addr: 0x" << std::hex << dram_addr << std::dec <<  std::endl;

          }

          std::uint64_t buf_queue_unique_id = this->queue_name_buf_unique_id_map.at(queue_name).at(r).at(c);
          std::uint64_t buf_instance_unique_id = queue_settings.prolog ?
            epoch_context.prolog_queue_name_fork_buf_unique_id_map.at(queue_name).at(r).at(c).at(conn_op_index) :
            buf_queue_unique_id; 

          int dram_prefetch_noc_id = 0;
          int y_coord = 0xFF;
          int x_coord = 0xFF;
          std::string prolog_comment_str = "";

          int pre_post = 0;
          if (queue_settings.prolog && epoch_context.op_queue_output_map.at(queue_name).size()) {
            std::string op_name = epoch_context.op_queue_output_map.at(queue_name)[conn_op_index];
            tt_op_info op_info = epoch_context.op_info_map.at(op_name);

            const bool has_two_step_prolog =
                epoch_context.prolog_tm_pipes_map.find(op_name) != epoch_context.prolog_tm_pipes_map.end() and
                epoch_context.prolog_tm_pipes_map.at(op_name).find(queue_name) !=
                    epoch_context.prolog_tm_pipes_map.at(op_name).end();

            bool has_two_step_prolog_operand = epoch_context.prolog_post_tm_operand.find(op_name) != epoch_context.prolog_post_tm_operand.end() and
                epoch_context.prolog_post_tm_operand.at(op_name).find(queue_name) !=
                    epoch_context.prolog_post_tm_operand.at(op_name).end();
            if (has_two_step_prolog_operand) {
                has_two_step_prolog_operand = epoch_context.prolog_post_tm_operand.at(op_name).at(queue_name);
            }
            if(has_two_step_prolog or has_two_step_prolog_operand){
                pre_post = 1;
            }

            prolog_comment_str = prolog_comment_str + ", prolog for op: " + op_name + " ";

            // FIXME imatosevic - this info should come from a function in Router
            // Also revise if we swap multicast directions for inputs in the new NOC allocation scheme. 
            bool inverse_r_buf_placement = false;
            bool inverse_c_buf_placement = false;
            if (n2p::get_op_class(op_info) == OpClass::MatMul || n2p::get_op_class(op_info) == OpClass::Depthwise) {
              int input_index = -1;
              for (int i = 0; i < (int)(op_info.input_names.size()); i++) {
                if (op_info.input_names[i] == queue_name) {
                  input_index = i;
                  break;
                }
              }
              TT_ASSERT(input_index != -1);
              inverse_r_buf_placement = op_info.grid_transpose ? (input_index == 0) : (input_index == 1);
            }
            
            int r_coord = r % op_info.grid_size_logical_r();
            if(r >= op_info.grid_size_logical_r()) {
                prolog_comment_str += "wrapping r_coord; ";
            }
            if (inverse_r_buf_placement) {
              r_coord = op_info.grid_size_logical_r() - 1 - r_coord;
            }

            int c_coord = c % op_info.grid_size_logical_c();
            if(c >= op_info.grid_size_logical_c()) {
                prolog_comment_str += "wrapping c_coord; ";
            }
            if (inverse_c_buf_placement) {
              c_coord = op_info.grid_size_logical_c() - 1 - c_coord;
            }

            op_info.get_core_yx_coord(r_coord, c_coord, y_coord, x_coord);
            
            dram_prefetch_noc_id = get_dram_prefetch_noc_id(queue_name, op_name);
          }

          out << YAML::BeginMap;
          out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(buf_instance_unique_id)));
          out << YAML::Comment("Queue " + queue_name + ": r = " + std::to_string(r) + ", c = " + std::to_string(c) + prolog_comment_str);
          out << YAML::Value << YAML::BeginMap;

          out << SET_KEY_VAL("md_op_name", queue_name);
          out << SET_KEY_VAL("buffer_type", c_buffer_type);
          out << SET_KEY_VAL("id", 0);
          out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(buf_instance_unique_id));
          out << SET_KEY_VAL("epoch_tiles", buf_epoch_tiles);
          out << SET_KEY_VAL(
                             "chip_id",
                             YAML::Flow << YAML::BeginSeq << epoch_context.buffer_map.at(buf_queue_unique_id).core_location().chip << YAML::EndSeq);
          out << SET_KEY_VAL("core_coordinates", SET_COORD2(y_coord, x_coord));
          out << SET_KEY_VAL("size_tiles", q_slot_size_tiles);
          out << SET_KEY_VAL("scatter_gather_num_tiles", epoch_context.op_queue_output_buf_granularity.at(queue_name));
          out << SET_KEY_VAL("tiles_per_input", tiles_per_input);       
          if(not epoch_context.op_queue_output_scatter.at(queue_name) and queue_settings.prolog and pre_post == 1) {
            out << SET_KEY_VAL("is_scatter", 1);
            out << SET_KEY_VAL("replicate", 1);
          }
          else  {
            out << SET_KEY_VAL("is_scatter", static_cast<int>(epoch_context.op_queue_output_scatter.at(queue_name)));
            out << SET_KEY_VAL("replicate", replicate);
          }
          out << SET_KEY_VAL("tile_size", tile_size_bytes);
          out << SET_KEY_VAL("dram_io_flag_is_remote", queue_is_remote);
          out << SET_KEY_VAL("dram_buf_flag", dram_buf_flag);
          out << SET_KEY_VAL("prefetch_type", pre_post);
          out << SET_KEY_VAL("dram_buf_streaming", 0);
          out << SET_KEY_VAL("write_dram_buf_flag", write_dram_buf_flag);
          out << SET_KEY_VAL("dram_prefetch_incoming_noc_id", dram_prefetch_noc_id);
          out << SET_KEY_VAL("dram_chan", dram_chan);
          out << SET_KEY_VAL("dram_sub_chan", (queue_info.loc == QUEUE_LOCATION::HOST ? 0: get_queue_dram_subchannel(buf_queue_unique_id, epoch_context)));  // WH only
          out << SET_KEY_VAL("dram_addr", uint_to_string(dram_addr));
          out << SET_KEY_VAL("q_slots", q_slots);
          out << SET_KEY_VAL("dram_io_flag", dram_io_flag);
          out << SET_KEY_VAL("dram_io_allow_overwrite", dram_io_allow_overwrite);
          out << SET_KEY_VAL("dram_io_skip_flow_ctrl", dram_io_skip_flow_ctrl);
          out << SET_KEY_VAL("dram_ram_flag", dram_ram_flag);
          out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
          out << SET_KEY_VAL("ublock_rt", (external_input ? 0 : input_op_info.output_dim.ublock_rt));
          out << SET_KEY_VAL("ublock_ct", (external_input ? 0 : input_op_info.output_dim.ublock_ct));
          out << SET_KEY_VAL("mblock_m", (external_input ? 0 : input_op_info.output_dim.mblock_m));
          out << SET_KEY_VAL("mblock_n", (external_input ? 0 : input_op_info.output_dim.mblock_n));
          out << SET_KEY_VAL("mblock_k", (external_input ? 0 : input_op_info.attributes.m_k));

          if (is_name_hw_tilize(queue_name, epoch_context)) {
            out << SET_KEY_VAL("hw_tilize", 1);
            std::string consumer_op_name = epoch_context.op_queue_output_map.at(queue_name).at(0);
            tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
            int core_r_div = consumer_op_info.grid_size_logical_r() / queue_info.grid_size.r;
            int core_c_div = consumer_op_info.grid_size_logical_c() / queue_info.grid_size.c;
            int row_size_per_core = (queue_info.dim.mblock_n/core_c_div) * queue_info.dim.ublock_ct;
            int row_size = queue_info.grid_size.c * row_size_per_core * core_c_div;
            out << SET_KEY_VAL("embedding_table_row_size", row_size);
            out << SET_KEY_VAL("embedding_table_row_size_per_core", row_size_per_core);
            out << SET_KEY_VAL("embedding_table_core_c_div", core_c_div);
            int tile_dim_r = tt::tile_dim_to_array(queue_info.tile_dim)[0];
            int tile_dim_c = tt::tile_dim_to_array(queue_info.tile_dim)[1];
            out << SET_KEY_VAL("untilized_output_tile_dim_r", tile_dim_r);
            out << SET_KEY_VAL("untilized_output_tile_dim_c", tile_dim_c);
            out << SET_KEY_VAL("untilized_output_c_dim", queue_info.dim.mblock_n/core_c_div);
            int tilize_mblock_n_loop_num_rows = queue_info.dim.ublock_order == Dim::R ? queue_info.dim.ublock_rt * tile_dim_r : (queue_info.dim.mblock_m/core_r_div) * queue_info.dim.ublock_rt * tile_dim_r;
            out << SET_KEY_VAL("tilize_mblock_n_loop_num_rows", tilize_mblock_n_loop_num_rows);
          }
          if (is_name_embedding_table_queue(queue_name, epoch_context)) {
            out << SET_KEY_VAL("embedding_table", 1);
            std::string consumer_op_name = epoch_context.op_queue_output_map.at(queue_name).at(0);
            tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
            int core_c_div = consumer_op_info.grid_size_logical_c() / queue_info.grid_size.c;
            int row_size_per_core = (queue_info.dim.mblock_n/core_c_div) * queue_info.dim.ublock_ct;
            int row_size = queue_info.grid_size.c * row_size_per_core * core_c_div;
            out << SET_KEY_VAL("embedding_table_row_size", row_size);
            out << SET_KEY_VAL("embedding_table_row_size_per_core", row_size_per_core);
            out << SET_KEY_VAL("embedding_table_core_c_div", core_c_div);
            int tile_dim_r = tt::tile_dim_to_array(queue_info.tile_dim)[0];
            int tile_dim_c = tt::tile_dim_to_array(queue_info.tile_dim)[1];
            out << SET_KEY_VAL("untilized_output_tile_dim_r", tile_dim_r);
            out << SET_KEY_VAL("untilized_output_tile_dim_c", tile_dim_c);
            out << SET_KEY_VAL("untilized_output_c_dim", queue_info.dim.mblock_n/core_c_div);
          }
          if (is_name_embedding_index_queue(queue_name, epoch_context)) {
            int indices_per_tile = tt::tile_dim_to_array(queue_info.tile_dim)[0] * tt::tile_dim_to_array(queue_info.tile_dim)[1];
            out << SET_KEY_VAL("embedding_index", 1);
            out << SET_KEY_VAL("embedding_indices_per_tile", indices_per_tile);
            std::string consumer_op_name = epoch_context.op_queue_output_map.at(queue_name).at(0);
            tt_op_info consumer_op_info = epoch_context.op_info_map.at(consumer_op_name);
            int indices_per_input = consumer_op_info.attributes.num_indices;
            out << SET_KEY_VAL("embedding_indices_per_input", indices_per_input);
          }
          if (external_input) {
            emit_untilize_output(out, NULL);
          } else {
            emit_untilize_output(out, &input_op_info);
          }

          out << YAML::EndMap;
          out << YAML::EndMap;
        }
      }
    }
}

int Net2Pipe::compute_intermediate_buffer_size_tiles(const tt_op_info &op_info, int input_count, int int_id, int output_is_scatter, int output_size_tiles, int output_replicate) const {
    int int_size_tiles;
    bool output_buf_shared_with_int = n2p::op_output_buffer_shared_with_intermediate(op_info);
    int mblock_tiles = get_mblock_size_tiles(op_info);

    if (output_buf_shared_with_int) {
        if (op_info.gradient_op)
            int_size_tiles = op_info.output_dim.t*mblock_tiles;
        else if (output_is_scatter)
            int_size_tiles = output_size_tiles*output_replicate;
        else
            int_size_tiles = output_size_tiles;
    } else if (n2p::op_is_fused(op_info)) {
        int_size_tiles = op_info.fused_op_info.intermed_buff_size_in_tiles[int_id];
        // It can happen that fused op declares that it's using more intermed buffer than it actually does.
        // In this case analysis of the fused op will fail to provide non zero values for tile count for intermed buffers
        // which are declared as used but are not actually used.
        // Previous version of the code acidentally was returning 1 (as it was doing "int_dim.ublock_rt *
        // int_dim.ublock_ct; where both values defaulted to -1 and return 1 in the end")
        // Later stages of the code relied on this behaviour, so here we return 1 for backward compatibility.
        int_size_tiles = std::max(int_size_tiles, 1);
    } else if (n2p::op_is_topk(op_info)){
        int_size_tiles = op_info.input_dims[0].mblock_m * op_info.input_dims[0].mblock_n *
                         op_info.input_dims[0].ublock_ct * op_info.input_dims[0].ublock_rt;
    } else {
        int_size_tiles = op_info.gradient_op ? op_info.output_dim.t * mblock_tiles : mblock_tiles;
    }

    return int_size_tiles;
}

void Net2Pipe::emit_kernel_bufs(
    YAML::Emitter &out,
    const temporal_epoch_context& epoch_context,
    const n2p::DeterministicKeyMap& deterministic_id_map,
    const std::string &op_name,
    int input_count
) const {
    tt_op_info op_info = epoch_context.op_info_map.at(op_name);

    if (epoch_context.prolog_buffers_per_op.find(op_name) != epoch_context.prolog_buffers_per_op.end()) {
        for (const auto& buf : epoch_context.prolog_buffers_per_op.at(op_name)) {
            emit_single_prolog_buffer(out, deterministic_id_map, buf);
        }
    }

    bool is_ethernet_datacopy = netlist_utils::is_valid_ethernet_op(op_info.type);
    auto get_op_input_buffer_id = [this](const std::string &op_name, const tt_op_info &op_info, const temporal_epoch_context& epoch_context, int i, int j, int k) {
        std::uint64_t buffer_id = epoch_context.op_input_buf_map.at(op_name).at(i).at(j).at(k);
        if (netlist_utils::is_valid_ethernet_op(op_info.type)) {
            while(epoch_context.buffer_output_pipes.find(buffer_id) != epoch_context.buffer_output_pipes.end()) {
                buffer_id = epoch_context.pipes.at(*epoch_context.buffer_output_pipes.at(buffer_id).begin()).output_buffer_ids().at(0);
            }
        }
        return buffer_id;
    };

    auto get_op_core_routing_cxy = [this](const std::string &op_name, const tt_op_info &op_info, int r, int c, bool is_ethernet_datacopy, int &y_coord, int &x_coord) -> void {
        if (is_ethernet_datacopy) {
            chip_id_t op_chip = op_graph_map.at(op_name).target_device;
            int dest_channel = op_info.attributes.ethernet_datacopy_attr.egress_channels.at(r * op_info.grid_size.c + c);
            const tt_xy_pair channel_xy = this->soc_descriptors.at(op_chip).ethernet_cores.at(dest_channel);
            y_coord = channel_xy.y;
            x_coord = channel_xy.x;
        } else {
            op_info.get_core_yx_coord(r, c, y_coord, x_coord);
        }
    };  

    for (int i = 0; i < op_info.grid_size_logical_r(); i++) {
        for (int j = 0; j < op_info.grid_size_logical_c(); j++) {
            int y_coord, x_coord;
            get_op_core_routing_cxy(op_name,op_info, i, j, is_ethernet_datacopy, y_coord, x_coord);
            int num_input_bufs = n2p::get_op_num_input_bufs(op_info);

            for (int k = 0; k < num_input_bufs; k++) {
                std::uint64_t input_unique_id = get_op_input_buffer_id(op_name, op_info, epoch_context, i, j, k);
                chip_id_t chip = epoch_context.buffer_map.at(input_unique_id).core_location().chip;

                int input_id;
                int input_tile_size_bytes;
                int input_epoch_tiles;
                int input_size_tiles;
                int input_scatter_gather_num_tiles;
                int input_tile_clear_granularity;
                int input_overlay_blob_size;
                get_op_input(
                    op_info,
                    epoch_context,
                    i,
                    j,
                    input_count,
                    k,
                    input_id,
                    input_overlay_blob_size,
                    input_tile_size_bytes,
                    input_epoch_tiles,
                    input_size_tiles,
                    input_scatter_gather_num_tiles,
                    input_tile_clear_granularity);
                input_size_tiles = epoch_context.buffer_map.at(input_unique_id).info().allocated_size_in_tiles();

                out << YAML::BeginMap;
                out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(input_unique_id)));
                out << YAML::Comment(
                    "Op " + op_info.name + ": [r=" + std::to_string(y_coord) + ", c=" + std::to_string(x_coord) +
                    "], kernel input buf " + std::to_string(k));
                out << YAML::Value << YAML::BeginMap;
                const auto &input_buffer = epoch_context.buffer_map.at(input_unique_id);
                int input_operand_id = input_buffer.info().stream_id();
                TT_ASSERT(is_ethernet_datacopy || input_operand_id == input_id);
                out << SET_KEY_VAL("md_op_name", op_name);
                out << SET_KEY_VAL("buffer_type", n2p::c_Unpacker);
                out << SET_KEY_VAL("id", input_operand_id);
                out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(input_unique_id));
                out << SET_KEY_VAL("epoch_tiles", input_epoch_tiles);
                out << SET_KEY_VAL("tiles_per_input", input_epoch_tiles/input_count);
                if (is_ethernet_datacopy) {
                    out << SET_KEY_VAL("ethernet_chan", this->soc_descriptors.at(chip).get_channel_of_ethernet_core(tt_xy_pair(x_coord, y_coord)));
                }
                out << SET_KEY_VAL(
                    "chip_id",
                    YAML::Flow << YAML::BeginSeq << chip << YAML::EndSeq);
                out << SET_KEY_VAL("core_coordinates", SET_COORD2(y_coord, x_coord));
                out << SET_KEY_VAL("size_tiles", input_size_tiles);
                out << SET_KEY_VAL("scatter_gather_num_tiles", input_scatter_gather_num_tiles);
                out << SET_KEY_VAL("is_scatter", 0);
                out << SET_KEY_VAL("replicate", 0);
                out << SET_KEY_VAL("tile_size", input_tile_size_bytes);
                out << SET_KEY_VAL("tile_clear_granularity", input_tile_clear_granularity);
                if (n2p::get_op_class(op_info) == OpClass::Tilizer) {
                    std::string table_input_name = op_info.input_names[0];
                    tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
                    int core_r_div = op_info.grid_size_logical_r() / input_queue_info.grid_size.r;
                    int core_c_div = op_info.grid_size_logical_c() / input_queue_info.grid_size.c;
                    int row_size_per_core = (input_queue_info.dim.mblock_n/core_c_div) * input_tile_size_bytes;
                    int row_size = row_size_per_core * core_c_div;
                    int col_size_per_core = (input_queue_info.dim.mblock_m/core_r_div) * input_queue_info.dim.ublock_rt * tt::tile_dim_to_array(input_queue_info.tile_dim)[0];
                    int tilize_row_col_offset = (i/input_queue_info.grid_size.r)*col_size_per_core*row_size + (j/input_queue_info.grid_size.c)*row_size_per_core;
                    out << SET_KEY_VAL("tilize_row_col_offset", tilize_row_col_offset);
                }
                if (n2p::get_op_class(op_info) == OpClass::Embedding) {
                  if (k == 0) {
                    out << SET_KEY_VAL("embedding_op_data_input", 1);
                  } else if (k == 1) {
                    out << SET_KEY_VAL("embedding_op_index_input", 1);
                  } else {
                    ERROR("Op: " + op_info.name + " is an embedding op with more than 2 inputs");
                  }
                }
                out << SET_KEY_VAL("dram_io_flag_is_remote", 0);
                out << SET_KEY_VAL("overlay_blob_size", input_overlay_blob_size);
                out << SET_KEY_VAL("dram_buf_flag", 0);
                out << SET_KEY_VAL("dram_buf_streaming", 0);
                out << SET_KEY_VAL("write_dram_buf_flag", 0);
                out << SET_KEY_VAL("dram_chan", 0);
                out << SET_KEY_VAL("dram_sub_chan", 0);
                out << SET_KEY_VAL("dram_addr", 0);
                out << SET_KEY_VAL("q_slots", 0);
                out << SET_KEY_VAL("dram_io_flag", 0);
                out << SET_KEY_VAL("dram_io_allow_overwrite", 0);
                out << SET_KEY_VAL("dram_io_skip_flow_ctrl", 0);
                out << SET_KEY_VAL("dram_ram_flag", 0);
                out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
                out << SET_KEY_VAL("ublock_rt", 0);
                out << SET_KEY_VAL("ublock_ct", 0);
                out << SET_KEY_VAL("mblock_m", 0);
                out << SET_KEY_VAL("mblock_n", 0);
                out << SET_KEY_VAL("mblock_k", 0);
                emit_untilize_output(out, NULL);

                out << YAML::EndMap;
                out << YAML::EndMap;
            }


            int mblock_k = 1;

            if(n2p::get_op_class(op_info) == OpClass::MatMul ) {
                if(op_info.attributes.identity){
                    mblock_k = std::numeric_limits<std::uint16_t>::max()-2; // For identity matmul m_k is dynamically updated
                
                // disable matmul l1 acc to have a0/b0 parity
                //} else if(op_info.arch_name == tt::ARCH::WORMHOLE_B0 && !n2p::is_bfp_format(op_info.output_data_format)) {
                //    mblock_k = 1; //For l1 accumulate kernel, only one push to intermediate buffer, bfp output currently not supported
                } else {
                    mblock_k = op_info.attributes.m_k * (op_info.attributes.accumulate ? op_info.attributes.z : 1);
                }
            } else if (n2p::get_op_class(op_info) == OpClass::Reduce) {
                mblock_k = op_info.attributes.z;
            } else if (n2p::get_op_class(op_info) == OpClass::Depthwise) {
                mblock_k = op_info.attributes.m_k;
            }

            if (op_info.attributes.bias) {
                // For int32 we need to load bias directly to dest before accumulation starts
                if (op_info.dest_accumulate_data_format != DataFormat::Int32) {
                    mblock_k+=1; // add additional pass through the interm buffer for bias add
                }

                if (op_info.attributes.requant || op_info.attributes.dequant){
                    mblock_k+=1; // add additional pass through the interm buffer for requant/dequant of int32 output
                }    
            }    

            int output_is_scatter;
            int output_size_tiles;
            int output_replicate;
            if (n2p::op_has_output_buf(op_info)) {
                // output is always in the form of a single buffer entry in pipegen.yaml
                // that may be implicitly multiplied for scatter buffers
                int output_id;
                int output_tile_size_bytes;
                int output_epoch_tiles;
                int output_scatter_gather_num_tiles;
                int output_tiles_per_input;
                get_op_output(
                    op_info,
                    input_count,
                    epoch_context,
                    i,
                    j,
                    output_id,
                    output_is_scatter,
                    output_replicate,
                    output_tile_size_bytes,
                    output_epoch_tiles,
                    output_size_tiles,
                    output_scatter_gather_num_tiles,
                    output_tiles_per_input);

                std::uint64_t output_unique_id = epoch_context.op_output_buf_map.at(op_name).at(i).at(j);
                chip_id_t chip = epoch_context.buffer_map.at(output_unique_id).core_location().chip;
                const auto &output_buf = epoch_context.buffer_map.at(output_unique_id);
                output_size_tiles = output_buf.info().allocated_size_in_tiles();
                TT_ASSERT(output_is_scatter == static_cast<int>(epoch_context.op_queue_output_scatter.at(op_name)));
                TT_ASSERT(output_scatter_gather_num_tiles == epoch_context.op_queue_output_buf_granularity.at(op_name));

                std::uint64_t int_unique_id = 0;
                bool output_buf_shared_with_int = n2p::op_output_buffer_shared_with_intermediate(op_info);
                if (output_buf_shared_with_int) {
                   TT_ASSERT(is_ethernet_datacopy || (n2p::op_num_intermediate_buf(op_info)==1)); //Output and interm buffer space can be shared only if op has 
                }
                bool override_to_force_scatter_operating_mode = is_ethernet_datacopy; 
                if (override_to_force_scatter_operating_mode) {
                    output_is_scatter = true;
                    output_replicate = output_buf.info().replication_factor();
                }
                
                if (n2p::op_has_intermediate_buf(op_info)) {
                    int_unique_id = epoch_context.op_intermediate_buf_map.at(op_name).at(i).at(j).at(0);
                }

                out << YAML::BeginMap;
                out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(output_unique_id)));
                out << YAML::Comment(
                    "Op " + op_info.name + ": [r=" + std::to_string(y_coord) + ", c=" + std::to_string(x_coord) +
                    "], kernel output buf");
                out << YAML::Value << YAML::BeginMap;

                out << SET_KEY_VAL("md_op_name", op_name);
                out << SET_KEY_VAL("buffer_type", n2p::c_Packer);
                const auto &output_buffer = epoch_context.buffer_map.at(output_unique_id);
                int output_operand_id = output_buffer.info().stream_id();
                TT_ASSERT(is_ethernet_datacopy || output_operand_id == output_id);
                out << SET_KEY_VAL("id", output_operand_id);
                out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(output_unique_id));
                out << SET_KEY_VAL("epoch_tiles", output_epoch_tiles);
                if (is_ethernet_datacopy) {
                    out << SET_KEY_VAL("ethernet_chan", this->soc_descriptors.at(chip).get_channel_of_ethernet_core(tt_xy_pair(x_coord, y_coord)));
                }
                out << SET_KEY_VAL("tiles_per_input", output_tiles_per_input);
                out << SET_KEY_VAL(
                    "chip_id",
                    YAML::Flow << YAML::BeginSeq << chip << YAML::EndSeq);
                out << SET_KEY_VAL("core_coordinates", SET_COORD2(y_coord, x_coord));
                out << SET_KEY_VAL("size_tiles", output_size_tiles);
                out << SET_KEY_VAL("scatter_gather_num_tiles", output_scatter_gather_num_tiles);
                out << SET_KEY_VAL("is_scatter", static_cast<int>(output_is_scatter));
                out << SET_KEY_VAL("replicate", output_replicate);
                out << SET_KEY_VAL("tile_size", output_tile_size_bytes);
                out << SET_KEY_VAL("dram_io_flag_is_remote", 0);
                out << SET_KEY_VAL("dram_buf_flag", 0);
                out << SET_KEY_VAL("dram_buf_streaming", 0);
                out << SET_KEY_VAL("write_dram_buf_flag", 0);
                out << SET_KEY_VAL("dram_chan", 0);
                out << SET_KEY_VAL("dram_sub_chan", 0);
                out << SET_KEY_VAL("dram_addr", 0);
                out << SET_KEY_VAL("q_slots", 0);
                out << SET_KEY_VAL("dram_io_flag", 0);
                out << SET_KEY_VAL("dram_io_allow_overwrite", 0);
                out << SET_KEY_VAL("dram_io_skip_flow_ctrl", 0);
                out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
                if (output_buf_shared_with_int) {
                    std::uint64_t shared_buffer_id = is_ethernet_datacopy ? get_op_input_buffer_id(op_name, op_info, epoch_context, i, j, 0) : int_unique_id;
                    out << SET_KEY_VAL("buffer_space_shared", deterministic_id_map.get_deterministic_key(shared_buffer_id));
                }
                out << SET_KEY_VAL("ublock_rt", op_info.output_dim.ublock_rt);
                out << SET_KEY_VAL("ublock_ct", op_info.output_dim.ublock_ct);
                out << SET_KEY_VAL("mblock_m", op_info.output_dim.mblock_m);
                out << SET_KEY_VAL("mblock_n", op_info.output_dim.mblock_n);
                out << SET_KEY_VAL("mblock_k", mblock_k);
                emit_untilize_output(out, &op_info);

                out << YAML::EndMap;
                out << YAML::EndMap;
            }

            if (n2p::op_has_intermediate_buf(op_info)) {
                for (int k = 0; k < n2p::op_num_intermediate_buf(op_info); k++) {
                    // FIXME: Update to grab directly from the buffer instead (intermediate_buffer.info().<>)
                    std::uint64_t int_unique_id = epoch_context.op_intermediate_buf_map.at(op_name).at(i).at(j).at(k);
                    const auto &intermediate_buffer = epoch_context.buffer_map.at(int_unique_id);

                    int int_tile_size_bytes = intermediate_buffer.info().tile_size_in_bytes();
                    int int_epoch_tiles = intermediate_buffer.info().total_epoch_tiles();
                    int int_size_tiles = intermediate_buffer.info().allocated_size_in_tiles();
                    int int_scatter_gather_num_tiles = intermediate_buffer.info().scatter_gather_num_tiles();
                
                    int dram_prefetch_noc_id = 0;
                    int dram_ram_flag = 0;
                    int dram_io_skip_flow_ctrl = 0;
                    int dram_io_allow_overwrite = 0;
                    int dram_chan = 0;
                    std::uint64_t dram_addr = 0;
                    int dram_buf_flag = 0;
                    int write_dram_buf_flag = 0;
                    int q_slots = 0;

                    int int_operand_id = intermediate_buffer.info().stream_id();
                    TT_ASSERT(int_operand_id == 24+(n2p::op_num_intermediate_buf(op_info)-1-k));

                    if (op_info.gradient_op) {
                        if (epoch_context.op_queue_output_map.at(op_info.name).size() != 1) {
                            ERROR("Op: " + op_info.name + " is a gradient op, must have a single queue output");
                        }
                        std::string queue_name = *(epoch_context.op_queue_output_map.at(op_info.name).begin());
                        if (!this->parsed_netlist.queue_map.count(queue_name)) {
                            ERROR("Op: " + op_info.name + " has non-queue output " + queue_name);
                        }
                        tt_queue_info queue_info = this->parsed_netlist.queue_map.at(queue_name);
                        dram_prefetch_noc_id = get_dram_prefetch_noc_id(queue_name, op_name);  
                        dram_ram_flag = (queue_info.type == IO_TYPE::RandomAccess);
                        dram_io_skip_flow_ctrl = (queue_info.type == IO_TYPE::RandomAccess);
                        dram_io_allow_overwrite = (queue_info.type == IO_TYPE::RandomAccess);
                        dram_chan = get_dram_buf_chan(queue_name, i, j);
                        dram_addr = get_dram_buf_addr(queue_name, i, j);
                        if (!(epoch_context.queue_setting_map.at(queue_name).prolog && epoch_context.queue_setting_map.at(queue_name).epilog)) {
                            ERROR(
                                "Queue: " + queue_name
                                << ", op " << op_name
                                << " -> gradient_op using intermediate queue without prolog and epilog set");
                        }
                        dram_buf_flag = 1;
                        epoch_context.queue_setting_map.at(queue_name).prolog;
                        write_dram_buf_flag = epoch_context.queue_setting_map.at(queue_name).epilog;
                        q_slots = queue_info.entries;
                        mblock_k+=1; // add additional pass through the interm buffer for grad op
                    }

                    out << YAML::BeginMap;
                    out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(int_unique_id)));
                    out << YAML::Comment(
                        "Op " + op_info.name + ": [r=" + std::to_string(y_coord) + ", c=" + std::to_string(x_coord) +
                        "], kernel intermediate buf");
                    out << YAML::Value << YAML::BeginMap;

                    out << SET_KEY_VAL("md_op_name", op_name);
                    out << SET_KEY_VAL("buffer_type", (op_info.gradient_op ? 
                        n2p::c_GradientOp : n2p::c_Intermediate));
                    out << SET_KEY_VAL("id", int_operand_id);
                    out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(int_unique_id));
                    out << SET_KEY_VAL("epoch_tiles", int_epoch_tiles);
                    out << SET_KEY_VAL(
                        "chip_id",
                        YAML::Flow << YAML::BeginSeq << epoch_context.buffer_map.at(int_unique_id).core_location().chip << YAML::EndSeq);
                    out << SET_KEY_VAL("core_coordinates", SET_COORD2(y_coord, x_coord));
                    out << SET_KEY_VAL("size_tiles", int_size_tiles);
                    out << SET_KEY_VAL("scatter_gather_num_tiles", int_scatter_gather_num_tiles);
                    out << SET_KEY_VAL("is_scatter", 0);
                    out << SET_KEY_VAL("replicate", 0);
                    out << SET_KEY_VAL("tile_size", int_tile_size_bytes);
                    out << SET_KEY_VAL("dram_io_flag_is_remote", 0);
                    out << SET_KEY_VAL("dram_buf_flag", dram_buf_flag);
                    out << SET_KEY_VAL("dram_buf_streaming", 0);
                    out << SET_KEY_VAL("write_dram_buf_flag", write_dram_buf_flag);
                    out << SET_KEY_VAL("dram_prefetch_incoming_noc_id", dram_prefetch_noc_id);
                    out << SET_KEY_VAL("dram_chan", dram_chan);
                    out << SET_KEY_VAL("dram_sub_chan", 0);  // WH only
                    out << SET_KEY_VAL("dram_addr", uint_to_string(dram_addr));
                    out << SET_KEY_VAL("q_slots", q_slots);
                    out << SET_KEY_VAL("dram_io_flag", 0);
                    out << SET_KEY_VAL("dram_io_allow_overwrite", dram_io_allow_overwrite);
                    out << SET_KEY_VAL("dram_io_skip_flow_ctrl", dram_io_skip_flow_ctrl);
                    out << SET_KEY_VAL("dram_ram_flag", dram_ram_flag);
                    out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
                    out << SET_KEY_VAL("ublock_rt", op_info.output_dim.ublock_rt);
                    out << SET_KEY_VAL("ublock_ct", op_info.output_dim.ublock_ct);
                    out << SET_KEY_VAL("mblock_m", op_info.output_dim.mblock_m);
                    out << SET_KEY_VAL("mblock_n", op_info.output_dim.mblock_n);
                    out << SET_KEY_VAL("mblock_k", mblock_k); 
                    emit_untilize_output(out, NULL);

                    out << YAML::EndMap;
                    out << YAML::EndMap;
                }
            }
        }
    }
}

void Net2Pipe::create_prolog_buffers(const std::string &op_name, int input_index, temporal_epoch_context& epoch_context) const {
    std::vector<n2p::prolog_buffer>& result = epoch_context.prolog_buffers_per_op[op_name];

    const string op_input_name = epoch_context.op_input_name_map[op_name][input_index];
    const n2p::prolog_layout& layout = epoch_context.prolog_layout_per_op.at(op_name).at(op_input_name);
    
    const int num_mcast_lines = layout.num_cores_to_mcast;

    //TODO: max chunks calc is wrong for the general multiple chunk case
    //const int gather_cores = layout.num_cores_r;
    const int max_chunks_per_core = 1; //layout.num_chunks_to_gather / gather_cores;

    const tt_op_info& op_info = epoch_context.op_info_map.at(op_name);
    const TileDim input_tile_dims = op_info.input_tile_dims.at(input_index);
    const int tile_size_bytes = n2p::get_format_tile_size_bytes(op_info.input_data_formats.at(input_index), true, input_tile_dims);

    const int chip_id = this->op_graph_map.at(op_name).target_device;

    for (int d = 0; d < num_mcast_lines; d++) {
      for (int chunk = 0; chunk < layout.num_chunks_to_gather; chunk++) {
        std::uint64_t unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
        log_debug(tt::LogNet2Pipe, "Creating prolog intermediate buffer id: {}", unique_id);

        const auto [core_r, core_c] = op_info.get_core_yx_coord(chunk / max_chunks_per_core, d);

        result.push_back(
          {
            .comment = "prolog intermediate buffer; input_index=" + std::to_string(input_index) + " chunk = " + std::to_string(chunk),
            .md_op_name = op_name,
            .operand_name = op_input_name,

            .uniqid = unique_id,
            .id = n2p::PROLOG_CHUNK_OPERAND_ID,
            
            .epoch_tiles = layout.chunk_size_tiles,
            .size_tiles = layout.chunk_size_tiles,
            .scatter_gather_num_tiles = layout.chunk_size_tiles,
            
            .tile_size = tile_size_bytes,

            .chip_id = chip_id,
            .core_r = core_r,
            .core_c = core_c,
          }
        );

        epoch_context.prolog_buffer_map[op_name][chunk][d][input_index] = unique_id;

        int core_log_y = core_r;
        int core_log_x = core_c;

        chip_id_t chip = this->op_graph_map.at(op_name).target_device;
        const auto &routing_core_coordinates = tt_cxy_pair(
            chip,
            this->soc_descriptors.at(chip).worker_log_to_routing_x.at(core_log_x),
            this->soc_descriptors.at(chip).worker_log_to_routing_y.at(core_log_y)
        );

        const auto &input_buffer_info = tt::buffer_info(
            tt::RouterBufferType::PrologInter, 
            0,// k, 
            0,//op_info.t, 
            layout.chunk_size_tiles,//input_size_tiles, 
            0,//op_info.ublock_rt, 
            0,//op_info.ublock_ct, 
            0,//input_count, 
            0,//op_info.mblock_m, 
            0,//op_info.mblock_n, 
            0,//input_scatter_gather_num_tiles,
            0,//input_epoch_tiles,
            0,
            op_info.input_data_formats.at(input_index),
            tt_xy_pair(n2p::get_tile_width(input_tile_dims), n2p::get_tile_height(input_tile_dims))
        );

        epoch_context.buffer_map.insert(
                {unique_id, router::router_buffer_info_t::create_immutable(routing_core_coordinates, input_buffer_info)});
        epoch_context.buffer_op_or_queue_name_map.insert({unique_id, op_input_name});
        /*
        log_debug(
            tt::LogNet2Pipe,
            "Adding input buffer id {} which belongs to op {} on input {}",
            input_unique_id,
            op_info.name,
            k
        );*/
      }
    }
}

void Net2Pipe::naive_place_unplaced_ethernet_datacopy_ops(const std::string &op_name, const temporal_epoch_context& epoch_context) const {
    // Op is unique per whole graph.
    tt_op_info &op_info = const_cast<tt_op_info&>(epoch_context.op_info_map.at(op_name));
    TT_ASSERT(netlist_utils::is_valid_ethernet_op(op_info.type));

    auto const &ingress_channels = op_info.attributes.ethernet_datacopy_attr.ingress_channels;
    auto const &egress_channels = op_info.attributes.ethernet_datacopy_attr.egress_channels;
    bool is_already_placed_on_endpoint_channels = ingress_channels.size() > 0;
    if (is_already_placed_on_endpoint_channels) {
        if (ingress_channels.size() != egress_channels.size()) {
            ERROR("Ethernet datacopy op " + op_name + " ingress_channels and egress_channels fields mismatch in number of entries. They must be the same size");
        }
        if (ingress_channels.size() != op_info.grid_size_x() * op_info.grid_size_y()) {
            ERROR("Ethernet datacopy op " + op_name + " specifies ingress_channels and/or egress_channels but the number of entries does not equal the total grid size of the op.");
        }

        return;
    }

    chip_id_t consumer_chip = op_info.attributes.ethernet_datacopy_attr.dest_device;

    const std::string &producer_op_name = op_info.input_names.at(0);
    chip_id_t producer_chip = this->op_graph_map.at(producer_op_name).target_device;

    const std::unordered_map<chip_id_t, eth_coord_t> &chip_locations = cluster_description->get_chip_locations();
    const auto &[producer_chip_x, producer_chip_y, producer_rack, producer_shelf] =
        chip_locations.at(producer_chip);
    const auto &[consumer_chip_x, consumer_chip_y, consumer_rack, consumer_shelf] =
        chip_locations.at(consumer_chip);


    auto consumer_left_channels = std::vector<ethernet_channel_t>{};
    auto consumer_right_channels = std::vector<ethernet_channel_t>{};
    auto consumer_up_channels = std::vector<ethernet_channel_t>{};
    auto consumer_down_channels = std::vector<ethernet_channel_t>{};

    const auto &chip_connections = cluster_description->get_ethernet_connections();
    const auto &consumer_chip_connections = chip_connections.at(consumer_chip);
    for (const auto &[consumer_channel, connected_endpoint] : consumer_chip_connections) {
      chip_id_t connected_chip = std::get<0>(connected_endpoint);

      const auto &[connected_chip_x, connected_chip_y, connected_rack, connected_shelf] =
          chip_locations.at(connected_chip);
      int delta_x = connected_chip_x - consumer_chip_x;
      if (delta_x > 0) {
        consumer_right_channels.push_back(consumer_channel);
        continue;
      } else if (delta_x < 0) {
        consumer_left_channels.push_back(consumer_channel);
        continue;
      }

      int delta_y = connected_chip_y - consumer_chip_y;
      if (delta_y > 0) {
        consumer_up_channels.push_back(consumer_channel);
        continue;
      } else if (delta_y < 0) {
        consumer_down_channels.push_back(consumer_channel);
        continue;
      }
    }

    auto producer_left_channels = std::vector<ethernet_channel_t>{};
    auto producer_right_channels = std::vector<ethernet_channel_t>{};
    auto producer_up_channels = std::vector<ethernet_channel_t>{};
    auto producer_down_channels = std::vector<ethernet_channel_t>{};

    const auto &producer_chip_connections = chip_connections.at(producer_chip);
    for (const auto &[producer_channel, connected_endpoint] : producer_chip_connections) {
      chip_id_t connected_chip = std::get<0>(connected_endpoint);

      const auto &[connected_chip_x, connected_chip_y, connected_rack, connected_shelf] =
          chip_locations.at(connected_chip);
      int delta_x = connected_chip_x - producer_chip_x;
      if (delta_x > 0) {
        producer_right_channels.push_back(producer_channel);
        continue;
      } else if (delta_x < 0) {
        producer_left_channels.push_back(producer_channel);
        continue;
      }

      int delta_y = connected_chip_y - producer_chip_y;
      if (delta_y > 0) {
        producer_up_channels.push_back(producer_channel);
        continue;
      } else if (delta_y < 0) {
        producer_down_channels.push_back(producer_channel);
        continue;
      }
    }

    auto choose_direction_channels = [](chip_id_t chip,
                                        const std::vector<ethernet_channel_t> &channels_up,
                                        const std::vector<ethernet_channel_t> &channels_right,
                                        const std::vector<ethernet_channel_t> &channels_down,
                                        const std::vector<ethernet_channel_t> &channels_left,
                                        int travel_x,  // positive is right, negative is left
                                        int travel_y   // positive is up, negative is down
                                        ) -> std::vector<ethernet_channel_t> {
        const auto &channels = (travel_x > 0)   ? channels_left
                               : (travel_x < 0) ? channels_right
                               : (travel_y > 0) ? channels_down
                                                : channels_up;
        if (channels.size() == 0) {
            if (channels_left.size() > 0) {
                return channels_left;
            } else if (channels_right.size() > 0) {
                return channels_right;
            } else if (channels_down.size() > 0) {
                return channels_down;
            } else if (channels_up.size() > 0) {
                return channels_up;
            } else {
                TT_ASSERT("Couldn't find any channels connected to {}", chip);
                return {};
            }
        } else {
            return channels;
        }
    };

    int producer_delta_x = producer_chip_x - consumer_chip_x;
    int producer_delta_y = producer_chip_y - consumer_chip_y;

    int consumer_delta_x = consumer_chip_x - producer_chip_x;
    int consumer_delta_y = consumer_chip_y - producer_chip_y;

    const auto &producer_channels = 
        choose_direction_channels(producer_chip,
                                  producer_up_channels,
                                  producer_right_channels,
                                  producer_down_channels,
                                  producer_left_channels,
                                  producer_delta_x,
                                  producer_delta_y);
    const auto &consumer_channels = 
        choose_direction_channels(consumer_chip,
                                  consumer_up_channels,
                                  consumer_right_channels,
                                  consumer_down_channels,
                                  consumer_left_channels,
                                  consumer_delta_x,
                                  consumer_delta_y);

    TT_ASSERT(producer_channels.size() > 0);
    TT_ASSERT(consumer_channels.size() > 0);

    int num_channels = op_info.grid_size.r * op_info.grid_size.c;

    for (int i = 0; i < num_channels; i++) {
      op_info.attributes.ethernet_datacopy_attr.ingress_channels.push_back(
          producer_channels.at(i % producer_channels.size()));
      op_info.attributes.ethernet_datacopy_attr.egress_channels.push_back(
          consumer_channels.at(i % consumer_channels.size()));
    }
}

void Net2Pipe::collect_kernel_buf_info(const std::string &op_name, int input_count, temporal_epoch_context& epoch_context) const {
    // Anonymous lambdas
    auto get_op_core_routing_cxy =
        [this](const std::string &op_name, const tt_op_info &op_info, int r, int c, bool is_ethernet_datacopy)
        -> std::variant<tt_cxy_pair, chip_id_t> {
        if (is_ethernet_datacopy) {
            bool channels_assigned = op_info.attributes.ethernet_datacopy_attr.egress_channels.size() > 0;
            chip_id_t ethernet_datacopy_dest_device = op_info.attributes.ethernet_datacopy_attr.dest_device;
            if (channels_assigned) {
                int dest_channel =
                    op_info.attributes.ethernet_datacopy_attr.egress_channels.at(r * op_info.grid_size.c + c);
                const tt_xy_pair channel_xy = this->soc_descriptors.at(ethernet_datacopy_dest_device).ethernet_cores.at(dest_channel);
                return tt_cxy_pair(ethernet_datacopy_dest_device, channel_xy.x, channel_xy.y);
            } else {
                return ethernet_datacopy_dest_device;
            }
        } else {
            int y_coord, x_coord;
            op_info.get_core_yx_coord(r, c, y_coord, x_coord);
            chip_id_t chip = this->op_graph_map.at(op_name).target_device;
            return tt_cxy_pair(
                chip,
                this->soc_descriptors.at(chip).worker_log_to_routing_x.at(x_coord),
                this->soc_descriptors.at(chip).worker_log_to_routing_y.at(y_coord));
        }
    };
    auto get_eth_datacopy_sender_chip_relay_buffer_location =
        [this](
            int row,
            int col,
            const tt_op_info &op_info,
            bool eth_channels_assigned,
            const buda_SocDescriptor &soc_descriptor) -> std::variant<tt_cxy_pair, chip_id_t> {
        chip_id_t eth_gather_src_device = this->op_graph_map.at(op_info.name).target_device;
        if (eth_channels_assigned) {
            int src_channel =
                op_info.attributes.ethernet_datacopy_attr.ingress_channels.at(col + row * op_info.grid_size.c);
            const auto &sender_buffer_routing_coordinates = soc_descriptor.ethernet_cores.at(src_channel);
            const auto &sender_buffer_location = tt_cxy_pair(eth_gather_src_device, sender_buffer_routing_coordinates);
            return sender_buffer_location;
        } else {
            return eth_gather_src_device;
        }
    };
    // end anonymous lambdas

    tt_op_info op_info = epoch_context.op_info_map.at(op_name);

    int num_input_bufs = n2p::get_op_num_input_bufs(op_info);
    for (int k = 0; k < num_input_bufs; k++) {
        const string& input_name = op_info.input_names.at(k);
        const bool has_two_step_prolog = epoch_context.prolog_tm_pipes_map.find(op_name) != epoch_context.prolog_tm_pipes_map.end() and
                                         epoch_context.prolog_tm_pipes_map.at(op_name).find(input_name) != epoch_context.prolog_tm_pipes_map.at(op_name).end();

        if(is_name_prolog_queue(input_name, epoch_context) and has_two_step_prolog) {
            this->create_prolog_buffers(op_name, k, epoch_context);
        }
        epoch_context.op_input_kernel_tile_clear_granularity[op_name][k] = this->get_op_kernel_input_tile_clear_granularity(op_info, k);
    }
    bool is_ethernet_datacopy = netlist_utils::is_valid_ethernet_op(op_info.type);

    for (int i = 0; i < op_info.grid_size_logical_r(); i++) {
        for (int j = 0; j < op_info.grid_size_logical_c(); j++) {
          
            int num_input_bufs = n2p::get_op_num_input_bufs(op_info);

            // Need to update for ethernet datacopy - should be the dest channel locations 
            const auto &routing_core_coordinates = get_op_core_routing_cxy(op_name, op_info, i, j, is_ethernet_datacopy);
            const bool routing_core_is_assigned = std::holds_alternative<tt_cxy_pair>(routing_core_coordinates);
            TT_ASSERT(routing_core_is_assigned || is_ethernet_datacopy, "Only ethernet datacopy op buffers can be unassigned to routing cores when collecting kernel buffer info");

            std::vector<std::uint64_t> input_buffers = {};
            for (int k = 0; k < num_input_bufs; k++) {
                int input_id;
                int input_tile_size_bytes;
                int input_epoch_tiles;
                int input_size_tiles; //TODO(snijjar): Needs to match output buffer size_tiles for ethernet datacopy
                int input_scatter_gather_num_tiles;
                int input_tile_clear_granularity;
                int input_overlay_blob_size;
                get_op_input(op_info, epoch_context, i, j, input_count, k, input_id, input_overlay_blob_size, input_tile_size_bytes, input_epoch_tiles,
                             input_size_tiles, input_scatter_gather_num_tiles,
                             input_tile_clear_granularity);
                std::uint64_t input_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
                epoch_context.op_input_buf_map[op_name][i][j][k] = input_unique_id;

                const TileDim input_tile_dim = op_info.input_tile_dims.at(k);
                // Need to set operand_id to -1 for ethernet datacopy because the operand ID is resolved later
                tt_xy_pair const &input_tile_dim_xy =
                    tt_xy_pair(n2p::get_tile_width(input_tile_dim), n2p::get_tile_height(input_tile_dim));
                const auto &input_buffer_info = tt::buffer_info(
                   tt::RouterBufferType::Input, 
                   is_ethernet_datacopy ? -1 : k, 
                   op_info.output_dim.t, 
                   input_size_tiles, 
                   op_info.output_dim.ublock_rt, 
                   op_info.output_dim.ublock_ct, 
                   input_count, 
                   op_info.output_dim.mblock_m, 
                   op_info.output_dim.mblock_n, 
                   input_scatter_gather_num_tiles,
                   input_epoch_tiles,
                   0,
                   op_info.input_data_formats.at(k),
                   input_tile_dim_xy
                   );

                log_debug(
                    tt::LogNet2Pipe,
                    "Adding input buffer id {} of size {} which belongs to op {} on input {}",
                    input_unique_id,
                    input_buffer_info.allocated_size_in_bytes(),
                    op_info.name,
                    k);
                const auto &input_buffer =
                    routing_core_is_assigned
                        ? (is_ethernet_datacopy
                               ? router::router_buffer_info_t::create_mutable(
                                     std::get<tt_cxy_pair>(routing_core_coordinates), input_buffer_info)
                               : router::router_buffer_info_t::create_immutable(
                                     std::get<tt_cxy_pair>(routing_core_coordinates), input_buffer_info))
                        : router::router_buffer_info_t::create_mutable(
                              std::get<chip_id_t>(routing_core_coordinates), input_buffer_info);
                epoch_context.buffer_map.insert({input_unique_id, input_buffer});
                epoch_context.buffer_op_or_queue_name_map.insert({input_unique_id, op_name});
                input_buffers.push_back(input_unique_id);

                // if ethernet datacopy, add the producer side relay buffers
                if (is_ethernet_datacopy) {
                    TT_ASSERT(k == 0);
                    bool channels_assigned = op_info.attributes.ethernet_datacopy_attr.ingress_channels.size() > 0;
                    chip_id_t eth_gather_src_device = this->op_graph_map.at(op_info.name).target_device;
                    const auto &sender_buffer_location = get_eth_datacopy_sender_chip_relay_buffer_location(
                        i, j, op_info, channels_assigned, this->soc_descriptors.at(eth_gather_src_device));

                    const auto &producer_side_buffer = channels_assigned
                                                           ? router::create_immutable_relay_buffer(
                                                                 1,
                                                                 input_scatter_gather_num_tiles,
                                                                 input_epoch_tiles,
                                                                 op_info.input_data_formats.at(0),
                                                                 input_tile_dim_xy,
                                                                 std::get<tt_cxy_pair>(sender_buffer_location))
                                                           : router::create_mutable_relay_buffer(
                                                                 1,
                                                                 input_scatter_gather_num_tiles,
                                                                 input_epoch_tiles,
                                                                 op_info.input_data_formats.at(0),
                                                                 input_tile_dim_xy,
                                                                 std::get<chip_id_t>(sender_buffer_location));
                    std::uint64_t buffer_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
                    if (channels_assigned) {
                        const auto &l = std::get<tt_cxy_pair>(sender_buffer_location);
                        log_debug(
                            tt::LogNet2Pipe,
                            "Ethernet datacopy producer side relay buffer id {} placed on chip={} y={} x={}",
                            buffer_unique_id,
                            l.chip,
                            l.y,
                            l.x);
                    } else {
                        log_debug(
                            tt::LogNet2Pipe,
                            "Ethernet datacopy producer side relay buffer id {} placed on chip={}",
                            buffer_unique_id,
                            std::get<chip_id_t>(sender_buffer_location));
                    }

                    // Override the input buffer ID in the op input buffer map to point to the ingress buffer on the
                    // producre chip so we can manage input pipe connections to the producer ethernet core properly
                    epoch_context.op_input_buf_map[op_name][i][j][k] = buffer_unique_id;
                    epoch_context.buffer_map.insert({buffer_unique_id, producer_side_buffer});
                    epoch_context.buffer_op_or_queue_name_map.insert({buffer_unique_id, op_name});

                    // connect the producer chip side buffer to the ethernet datacopy input buffer with a pipe
                    std::vector<std::uint64_t> pipe_inputs = {buffer_unique_id};
                    std::vector<std::uint64_t> pipe_outputs = {input_unique_id};
                    std::string pipe_name = "";  // op_info.name;
                    std::uint64_t pipe_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
                    log_debug(
                        tt::LogNet2Pipe,
                        "New pipe ({}) (internal to ethernet datacopy): input_buffer_ids: {}, output_buffer_ids: {}",
                        pipe_unique_id,
                        fmt::join(pipe_inputs, ", "),
                        fmt::join(pipe_outputs, ", "));
                    int tile_clear_granularity = this->get_op_kernel_input_tile_clear_granularity(op_info, 0);

                    const auto &eth_link_pipe =
                        channels_assigned
                            ? pipe_t(
                                  std::get<tt_cxy_pair>(routing_core_coordinates),
                                  pipe_inputs,
                                  pipe_outputs,
                                  pipe_name,
                                  0,
                                  tile_clear_granularity)
                            : pipe_t(pipe_inputs, pipe_outputs, pipe_name, 0, tile_clear_granularity);
                    epoch_context.pipes.insert({pipe_unique_id, eth_link_pipe});

                    register_pipe_as_output_of_buffers(pipe_unique_id, pipe_inputs, epoch_context);
                    register_pipe_as_input_of_buffers(pipe_unique_id, pipe_outputs, epoch_context);
                }
            }

            // output is always in the form of a single buffer entry in pipegen.yaml
            // that may be implicitly multiplied for scatter buffers
            int output_is_scatter;
            int output_replicate;
            int output_id;
            int output_tile_size_bytes;
            int output_epoch_tiles;
            int output_size_tiles;
            int output_scatter_gather_num_tiles;
            int output_tiles_per_input;
            get_op_output(
                op_info,
                input_count,
                epoch_context,
                i,
                j,
                output_id,
                output_is_scatter,
                output_replicate,
                output_tile_size_bytes,
                output_epoch_tiles,
                output_size_tiles,
                output_scatter_gather_num_tiles,
                output_tiles_per_input);

            epoch_context.op_queue_output_scatter[op_name] = static_cast<bool>(output_is_scatter);
            epoch_context.op_queue_output_buf_granularity[op_name] = output_scatter_gather_num_tiles;

            if (is_ethernet_datacopy) {
                if (!output_is_scatter) {
                    output_is_scatter = true;
                    output_replicate = 1;
                }

                /// We do this to force the "pack" and "unpack" of ethernet datacopy to be the same size, since they
                /// are supposed to be fully overlapping buffers (the same buffer, really)
                if (epoch_context.buffer_map.at(input_buffers.at(0)).mutable_info()._allocated_size_tiles != output_size_tiles) {
                    if(epoch_context.buffer_map.at(input_buffers.at(0)).mutable_info()._allocated_size_tiles < output_size_tiles) {
                        TT_ASSERT(output_size_tiles % epoch_context.buffer_map.at(input_buffers.at(0)).mutable_info()._allocated_size_tiles == 0);
                        epoch_context.buffer_map.at(input_buffers.at(0)).mutable_info()._allocated_size_tiles = output_size_tiles;
                    }
                    TT_ASSERT(epoch_context.buffer_map.at(input_buffers.at(0)).mutable_info()._allocated_size_tiles % output_size_tiles == 0);
                    output_size_tiles = epoch_context.buffer_map.at(input_buffers.at(0)).mutable_info()._allocated_size_tiles;
                }
            }
            // Pipegen expects the buffer id scheme for scatter
            int output_unique_id_block = output_is_scatter ? (n2p::get_op_output_mblock_tiles(op_info) * n2p::get_op_output_full_buf_size_mblocks(op_info)) : 1;
            std::uint64_t output_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN, output_unique_id_block);
            epoch_context.op_output_buf_map[op_name][i][j] = output_unique_id;

            // If it's an ethernet datacopy, we want a way to link the input/output buffers instead of the output/intermediate buffers
            // Need to set operand_id to -1 for ethernet datacopy because the operand_id is resolved later

            const TileDim output_tile_dim = op_info.output_tile_dim;
            const auto &output_buffer_info = tt::buffer_info(
                tt::RouterBufferType::Output, 
                is_ethernet_datacopy ? -1 : OUTPUT_BUFFER_STREAM_START, 
                op_info.output_dim.t, 
                output_size_tiles, 
                op_info.output_dim.ublock_rt, 
                op_info.output_dim.ublock_ct, 
                input_count, 
                op_info.output_dim.mblock_m, 
                op_info.output_dim.mblock_n, 
                output_scatter_gather_num_tiles,
                output_epoch_tiles,
                output_replicate,
                op_info.output_data_format,
                tt_xy_pair(n2p::get_tile_width(output_tile_dim), n2p::get_tile_height(output_tile_dim)),
                op_info.untilize_output,
                output_is_scatter);
                log_debug( 
                    tt::LogNet2Pipe,
                    "Adding output buffer id {} which belongs to op {} on output {}",
                output_unique_id,
                op_info.name,
                0);

            log_debug(
                tt::LogRouter,
                "Adding output buffer id {} of size {} B which belongs to op {} on output {}",
                output_unique_id,
                output_buffer_info.allocated_size_in_bytes(),
                op_info.name,
                0);
            const auto &output_buffer = routing_core_is_assigned ? 
                router::router_buffer_info_t::create_immutable(std::get<tt_cxy_pair>(routing_core_coordinates), output_buffer_info) :
                router::router_buffer_info_t::create_mutable(std::get<chip_id_t>(routing_core_coordinates), output_buffer_info);

            epoch_context.buffer_map.insert({output_unique_id, output_buffer});
            epoch_context.buffer_op_or_queue_name_map.insert({output_unique_id, op_name});

            if (output_is_scatter) {
              for (int rep = 1; rep < output_replicate; rep++) {
                log_debug( 
                tt::LogNet2Pipe,
                "Adding output buffer id {} which belongs to op {} on output {}",
                output_unique_id + rep*output_scatter_gather_num_tiles,
                op_info.name,
                0);

                const auto &output_scatter_buf_info = routing_core_is_assigned ?
                    router::router_buffer_info_t::create_immutable(std::get<tt_cxy_pair>(routing_core_coordinates), output_buffer_info) :
                    router::router_buffer_info_t::create_mutable(std::get<chip_id_t>(routing_core_coordinates), output_buffer_info);
                epoch_context.buffer_map.insert({output_unique_id + rep*output_scatter_gather_num_tiles, output_scatter_buf_info});
                epoch_context.buffer_op_or_queue_name_map.insert({output_unique_id + rep*output_scatter_gather_num_tiles, op_name});
              }
            }

            if (n2p::op_has_intermediate_buf(op_info)) {
                TT_ASSERT(!is_ethernet_datacopy);
                for (int k = 0; k < n2p::op_num_intermediate_buf(op_info); k++) {
                    int int_id = 24+(n2p::op_num_intermediate_buf(op_info)-1-k);
                    std::uint64_t int_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
                    epoch_context.op_intermediate_buf_map[op_name][i][j][k] = int_unique_id;

                    int mblock_tiles = 0;
                    DataFormat intermed_data_format = op_info.intermed_data_format;
                    if (n2p::op_is_topk(op_info)) {
                        mblock_tiles = op_info.input_dims[0].mblock_m * op_info.input_dims[0].mblock_n *
                                       op_info.input_dims[0].ublock_ct * op_info.input_dims[0].ublock_rt;
                        intermed_data_format = op_info.input_data_formats[n2p::op_num_intermediate_buf(op_info)-1-k];
                    } else {
                        mblock_tiles = n2p::get_op_output_mblock_tiles(op_info);
                    }
                    int int_epoch_tiles = op_info.gradient_op ? mblock_tiles : input_count * mblock_tiles;
                    int int_size_tiles = compute_intermediate_buffer_size_tiles(op_info, input_count, n2p::op_num_intermediate_buf(op_info)-1-k, output_is_scatter, output_size_tiles, output_replicate);
                    int int_scatter_gather_num_tiles = mblock_tiles;

                    TileDim intermediate_tile_dim = op_info.output_tile_dim;
                    const auto &intermediate_buffer_info = tt::buffer_info(
                        tt::RouterBufferType::Intermediate, 
                        int_id,
                        op_info.output_dim.t, 
                        int_size_tiles, 
                        op_info.output_dim.ublock_rt, 
                        op_info.output_dim.ublock_ct, 
                        input_count, 
                        op_info.output_dim.mblock_m, 
                        op_info.output_dim.mblock_n, 
                        int_scatter_gather_num_tiles,
                        int_epoch_tiles,
                        0,
                        intermed_data_format,
                        tt_xy_pair(n2p::get_tile_width(intermediate_tile_dim), n2p::get_tile_height(intermediate_tile_dim))
                        );
                    log_debug(
                        tt::LogRouter, "Adding intermediate buffer id {} of size {} which belongs to op {}", int_unique_id, intermediate_buffer_info.allocated_size_in_bytes(), op_info.name);
                    epoch_context.buffer_map.insert(
                        {int_unique_id, router::router_buffer_info_t::create_immutable(std::get<tt_cxy_pair>(routing_core_coordinates), intermediate_buffer_info)});
                    epoch_context.buffer_op_or_queue_name_map.insert({int_unique_id, op_name});
                }        
            }
        }
    }
}


bool Net2Pipe::producer_output_row_major_ublock_scan_order(std::string producer_name, const temporal_epoch_context& epoch_context) const {
  if (name_is_queue(producer_name)) {
    Dim ublock_order = epoch_context.queue_unique_id_info_map.at(epoch_context.queue_name_unique_id_map.at(producer_name)).dim.ublock_order;
    bool result = (ublock_order == Dim::R);
    return result;
  } else if (name_is_op(producer_name, epoch_context)) {
    tt_op_info producer_op_info = epoch_context.op_info_map.at(producer_name);
    bool result = (producer_op_info.output_dim.ublock_order == Dim::R);
    if ((n2p::get_op_class(producer_op_info) == OpClass::MatMul) && !result) {
      ERROR("Op: " + producer_op_info.name << " is a matmul but does not have ublock_order: r");
    } else if ((n2p::get_op_class(producer_op_info) == OpClass::Reduce) && !result) {
      ERROR("Op: " + producer_op_info.name << " is a reduce but does not have ublock_order: r");
    } else if ((n2p::get_op_class(producer_op_info) == OpClass::Depthwise) && !result) {
      ERROR("Op: " + producer_op_info.name << " is a depthwise but does not have ublock_order: r");
    }  
    return result;
  } else {
    ERROR(std::string("Unknown producer name: ") + producer_name);
  }
}

void Net2Pipe::emit_untilize_output(YAML::Emitter &out, const tt_op_info *op_info) const {
    int full_r_dim = 0;
    int full_c_dim = 0;
    int r_dim = 0;
    int c_dim = 0;
    int z_dim = 0;
    int type_0_zdim = 0;
    int type_1_zdim = 0;
    int untilized_output = 0;
    int tile_dim_r = 0;    
    int tile_dim_c = 0;

    if ((op_info != NULL) && op_info->untilize_output) {
        //  untilized_output: set to 1 to utilize
        // untilized_output_full_r_dim: r of final tensor
        // untilized_output_full_c_dim: c of final tensor
        // untilized_output_r_dim: r of tensor that's coming out of packer
        // untilized_output_c_dim: c of tensor that's coming out of packer
        // untilized_output_z_dim: z of tensor that's coming out of packer
        // untilized_output_type_0_zdim: 0 if not using type 1, otherwise its z dim of final tensor
        // untilized_output_type_1_zdim: 0 if not using type 2, otherwise its z dim of final tensor
        // r and c are specified in 32x32 blocks. I.e. if you have a tensor with 64 rows and 128 columns, then r will be
        // 64/32 = 2 and c will be 128/32 = 4. z counts faces, and faces will either be another batch or different
        // output zdim type.
        untilized_output = 1;
        full_r_dim = op_info->grid_size_logical_r() * op_info->output_dim.mblock_m * op_info->output_dim.ublock_rt;
        full_c_dim = op_info->grid_size_logical_c() * op_info->output_dim.mblock_n * op_info->output_dim.ublock_ct;
        r_dim = op_info->output_dim.mblock_m * op_info->output_dim.ublock_rt;
        c_dim = op_info->output_dim.mblock_n * op_info->output_dim.ublock_ct;
        z_dim = op_info->output_dim.t;
        type_0_zdim = 0;
        type_1_zdim = 0;
        tile_dim_r = tt::tile_dim_to_array(op_info->output_tile_dim)[0];
        tile_dim_c = tt::tile_dim_to_array(op_info->output_tile_dim)[1];
        out << SET_KEY_VAL("untilized_output_tile_dim_r", tile_dim_r);
        out << SET_KEY_VAL("untilized_output_tile_dim_c", tile_dim_c);
        out << SET_KEY_VAL("untilized_output_c_dim", c_dim);
    }

    out << SET_KEY_VAL("untilized_output", untilized_output);
    out << SET_KEY_VAL("untilized_output_full_r_dim", full_r_dim);
    out << SET_KEY_VAL("untilized_output_full_c_dim", full_c_dim);
    out << SET_KEY_VAL("untilized_output_r_dim", r_dim);
    out << SET_KEY_VAL("untilized_output_z_dim", z_dim);
    out << SET_KEY_VAL("untilized_output_type_0_zdim", type_0_zdim);
    out << SET_KEY_VAL("untilized_output_type_1_zdim", type_1_zdim);
}


void Net2Pipe::get_op_input(
    const tt_op_info &op_info,
    const temporal_epoch_context& epoch_context,
    int core_r,
    int core_c,
    int input_count,
    int index,
    int &id,
    int &overlay_blob_size,
    int &tile_size_bytes,
    int &epoch_tiles,
    int &size_tiles,
    int &scatter_gather_num_tiles,
    int &tile_clear_granularity) const {
    if (index >= n2p::get_op_num_input_bufs(op_info)) {
        ERROR("Op: " + op_info.name << ", illegal input index: " + std::to_string(index));
    }

    overlay_blob_size = op_info.overlay_size > 0 ? op_info.overlay_size : 0;

    id = index;
    // op input tiles always have header, unless we're reading from the embedding table
    bool embedding_op = (n2p::get_op_class(op_info) == OpClass::Embedding);
    bool tilizer_op = (n2p::get_op_class(op_info) == OpClass::Tilizer);
    if ((embedding_op || tilizer_op) && (index == 0)) {
      std::string table_input_name = op_info.input_names[index];
      if (is_name_embedding_table_queue(table_input_name, epoch_context) || is_name_hw_tilize(table_input_name, epoch_context)) {
        tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
        tile_size_bytes = n2p::get_format_tile_size_bytes(op_info.input_data_formats[index], false, input_queue_info.tile_dim) * input_queue_info.dim.ublock_ct / tt::tile_dim_to_array(input_queue_info.tile_dim)[0];
      } else {
        ERROR(std::string("Op ") + op_info.name + " is an embedding/tilizer op whose input 0 is not a support IO type");
      }      
    } else {
      tile_size_bytes = n2p::get_format_tile_size_bytes(op_info.input_data_formats[index], true, op_info.input_tile_dims[index]);
    }

    size_tiles = get_op_kernel_input_size_tiles(op_info, index, epoch_context);
    tile_clear_granularity = this->get_op_kernel_input_tile_clear_granularity(op_info, index);

    if (n2p::get_op_class(op_info) == OpClass::MatMul) {
        // k-dim in netlist is the total input size per core, i.e. producer output mblock dimension times producer full
        // row/column
        int input_block_size_tiles;
        int input_block_k_tiles = op_info.attributes.m_k * op_info.attributes.u_kt;
        int t = op_info.output_dim.t;
        if (op_info.attributes.identity) {
           if (index == 0) {
               input_block_size_tiles = op_info.attributes.num_sparse_tiles;
               scatter_gather_num_tiles = op_info.attributes.num_sparse_tiles;
               epoch_tiles = input_count * input_block_size_tiles;
           } else if (index == 1) {
               input_block_size_tiles = input_block_k_tiles * op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct;
               scatter_gather_num_tiles = op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
               //epoch_tiles = input_count * op_info.attributes.act_t * input_block_size_tiles;
               t*=op_info.attributes.accumulate ? op_info.attributes.z : 1;
               epoch_tiles = input_count * t * input_block_size_tiles;
           } else if (index == 2) {
               input_block_size_tiles = op_info.attributes.num_index_tiles;
               scatter_gather_num_tiles = op_info.attributes.num_index_tiles;
               epoch_tiles = input_count * input_block_size_tiles;
           } else {
               if (!op_info.attributes.bias)  {
                  ERROR(std::string("Op ") + op_info.name + " has 4th input but no bias attribute set ");
               }
               input_block_size_tiles = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct * op_info.output_dim.mblock_m * op_info.output_dim.mblock_n;
               scatter_gather_num_tiles = input_block_size_tiles;
               epoch_tiles = input_count * t * input_block_size_tiles;
           }
        } else {
           if (index == 0) {
               input_block_size_tiles = input_block_k_tiles * op_info.output_dim.mblock_m * op_info.output_dim.ublock_rt;
               scatter_gather_num_tiles = (op_info.attributes.min_input_buffer[0] ? 1 : op_info.output_dim.mblock_m) * op_info.output_dim.ublock_rt * op_info.attributes.u_kt;
               t*=op_info.attributes.accumulate ? op_info.attributes.z : 1;
           } else if (index == 1) {
               input_block_size_tiles = input_block_k_tiles * op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct;
               scatter_gather_num_tiles = (op_info.attributes.min_input_buffer[1] ? 1 : op_info.output_dim.mblock_n) * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
               t*=op_info.attributes.accumulate ? op_info.attributes.z : 1;
           } else if (index == 2) {
               if (!(op_info.attributes.bias || op_info.attributes.requant || op_info.attributes.dequant))  {
                  ERROR(std::string("Op ") + op_info.name + " has 3rd input but no bias or requant or dequant attribute set ");
               }
               input_block_size_tiles = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct * op_info.output_dim.mblock_m * op_info.output_dim.mblock_n;
               scatter_gather_num_tiles = input_block_size_tiles;
           } else {
               if (!((op_info.attributes.requant || op_info.attributes.dequant) && op_info.attributes.bias))   {
                  ERROR(std::string("Op ") + op_info.name + " has 4th input but requant or dequant and bias attributes are not set ");
               }
               input_block_size_tiles = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct * op_info.output_dim.mblock_m * op_info.output_dim.mblock_n;
               scatter_gather_num_tiles = input_block_size_tiles;
           }
           epoch_tiles = input_count * t * input_block_size_tiles;
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Depthwise) {
        int input_block_size_tiles;
        if (index == 0) {
            input_block_size_tiles = get_mblock_size_tiles(op_info) * op_info.attributes.m_k;
            scatter_gather_num_tiles = op_info.output_dim.mblock_m * op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
        } else if (index == 1) {
            input_block_size_tiles = op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct * op_info.attributes.m_k * op_info.attributes.u_kt; // u_kt == 1 for depthwise 
            scatter_gather_num_tiles = op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
        } else {
            if (!op_info.attributes.bias) {
                ERROR(std::string("Depthwise Op ") + op_info.name + " has more than 2 inputs, but no bias attributes set");
            }
            input_block_size_tiles = get_mblock_size_tiles(op_info);
            scatter_gather_num_tiles = input_block_size_tiles;
        }
        epoch_tiles = input_count * op_info.output_dim.t * input_block_size_tiles;
    } else if (n2p::get_op_class(op_info) == OpClass::Reduce) {
        int input_block_k_tiles = op_info.attributes.m_k * op_info.attributes.u_kt;
        int input_block_size_tiles;
        if (op_info.attributes.reduce_dim == Dim::R) {
            input_block_size_tiles = input_block_k_tiles * op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct;
            scatter_gather_num_tiles = op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
            epoch_tiles = input_count * op_info.output_dim.t * input_block_size_tiles;
        } else if (op_info.attributes.reduce_dim == Dim::C) {
            input_block_size_tiles = input_block_k_tiles * op_info.output_dim.mblock_m * op_info.output_dim.ublock_rt;
            scatter_gather_num_tiles = op_info.output_dim.ublock_rt * op_info.attributes.u_kt;
            epoch_tiles = input_count * op_info.output_dim.t * input_block_size_tiles;
        } else if (op_info.attributes.reduce_dim == Dim::Z) {
            int mblock_size_tiles = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct * op_info.output_dim.mblock_m * op_info.output_dim.mblock_n;
            epoch_tiles = input_count * (op_info.output_dim.t*op_info.attributes.z) * mblock_size_tiles; 
            scatter_gather_num_tiles = mblock_size_tiles;
        } else {
            TT_ASSERT(false, "unsupported reduce dim. only r, c and z are supported");
        }
    } else if (n2p::get_op_class(op_info) == OpClass::FusedOp) {
        string rel_input_name = "input" + to_string(index); //op_info.fused_op_info.input_names_to_rel_names_map[op_info.input_names.at(index)];
        std::unordered_map<std::string, tt_scheduled_op_info> input2op_map = epoch_context.fused_op_schedule_map.at(op_info.name);

        int mblock_m = input2op_map[rel_input_name].output_dim.mblock_m;
        int mblock_n = input2op_map[rel_input_name].output_dim.mblock_n;

        int ublock_rt = input2op_map[rel_input_name].output_dim.ublock_rt;
        int ublock_ct = input2op_map[rel_input_name].output_dim.ublock_ct;

        if (input2op_map[rel_input_name].type == "matmul") {
            int m_k = input2op_map[rel_input_name].m_k; 
            int u_kt = input2op_map[rel_input_name].u_kt; 
            int input_block_k_tiles = m_k * u_kt;
            int input_block_size_tiles;
            if (rel_input_name == input2op_map[rel_input_name].input_names.at(0)) {
                input_block_size_tiles = input_block_k_tiles * mblock_m * ublock_rt;
                scatter_gather_num_tiles = ublock_rt * u_kt;
            } else {
                input_block_size_tiles = input_block_k_tiles * mblock_n * ublock_ct;
                scatter_gather_num_tiles = m_k * ublock_ct * u_kt;
            }
            epoch_tiles = input_count * op_info.output_dim.t * input_block_size_tiles;
        } else if (input2op_map[rel_input_name].type == "reduce") {
            int m_k = input2op_map[rel_input_name].m_k; 
            int u_kt = input2op_map[rel_input_name].u_kt; 
            int input_block_k_tiles = m_k * u_kt;
            int input_block_size_tiles;
            if (input2op_map[rel_input_name].reduce_dim == Dim::R) {
                input_block_size_tiles = input_block_k_tiles * mblock_n * ublock_ct;
                scatter_gather_num_tiles = ublock_ct * u_kt;
            } else if (input2op_map[rel_input_name].reduce_dim == Dim::C) {
                input_block_size_tiles = input_block_k_tiles * mblock_m * ublock_rt;
                scatter_gather_num_tiles = ublock_rt * u_kt;
            } else {
                TT_ASSERT(false, "unsupported reduce dim in fused op. only r and c are supported");
            }    
            epoch_tiles = input_count * op_info.output_dim.t * input_block_size_tiles;
        } else {
            int mblock_size_tiles = ublock_rt * ublock_ct * mblock_m * mblock_n;
            epoch_tiles = input_count * op_info.output_dim.t * mblock_size_tiles;
            scatter_gather_num_tiles = mblock_size_tiles;
        }    
    } else if (n2p::get_op_class(op_info) == OpClass::Nary) {
        tt_dim_info input_dim = op_info.input_dims.at(index);
        if (op_info.attributes.splice_mode == SpliceMode::Ublock) {
            int mblock_size_tiles = input_dim.ublock_rt * input_dim.ublock_ct * input_dim.mblock_m * input_dim.mblock_n;
            epoch_tiles = input_count * op_info.output_dim.t * mblock_size_tiles; //input & output dims must match
            scatter_gather_num_tiles = mblock_size_tiles;
        } else if (op_info.attributes.splice_mode == SpliceMode::T) {
            int mblock_size_tiles = input_dim.ublock_rt * input_dim.ublock_ct * input_dim.mblock_m * input_dim.mblock_n;
            epoch_tiles = input_count * input_dim.t * mblock_size_tiles; //input & output dims must match
            scatter_gather_num_tiles = mblock_size_tiles;
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Buffer) {
        int input_block_size_tiles;
        if (index == 0) {
            int input_block_k_tiles = op_info.attributes.m_k * op_info.attributes.u_kt; // m_k == input mblock_m, input u_rt == u_kt
            input_block_size_tiles = input_block_k_tiles * op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct;
            scatter_gather_num_tiles = (op_info.attributes.min_input_buffer[1] ? 1 : op_info.output_dim.mblock_n) * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
            epoch_tiles = input_count * op_info.output_dim.t * input_block_size_tiles;
        } else {
            input_block_size_tiles = op_info.attributes.num_index_tiles;
            scatter_gather_num_tiles = op_info.attributes.num_index_tiles;
            epoch_tiles = input_count * input_block_size_tiles;
        }    
    } else if (n2p::get_op_class(op_info) == OpClass::Embedding) {
        int num_indices_per_core = op_info.attributes.num_indices / (op_info.grid_size_logical_r() * op_info.grid_size_logical_c());
        std::string table_input_name = op_info.input_names[index];
        tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
        if (index == 0) {
            // embedding table
            int core_c_div = op_info.grid_size_logical_c() / input_queue_info.grid_size.c;
            epoch_tiles = input_count * op_info.attributes.num_indices * (input_queue_info.dim.mblock_n/core_c_div);
            scatter_gather_num_tiles = op_info.attributes.num_indices;
        } else {
            // indexes: input not used by kernel, cleared by firmware as indexes are used
            int indices_per_tile = tt::tile_dim_to_array(input_queue_info.tile_dim)[0] * tt::tile_dim_to_array(input_queue_info.tile_dim)[1];
            int tiles_per_input = num_indices_per_core / indices_per_tile;
            if ((num_indices_per_core % indices_per_tile) != 0) {
              tiles_per_input++;
            }
            epoch_tiles = input_count * tiles_per_input;
            scatter_gather_num_tiles = tiles_per_input;
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Tilizer) {
        std::string table_input_name = op_info.input_names[index];
        tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
        int core_r_div = op_info.grid_size_logical_r() / input_queue_info.grid_size.r;
        int core_c_div = op_info.grid_size_logical_c() / input_queue_info.grid_size.c;
        int num_rows = (input_queue_info.dim.mblock_m/core_r_div) * input_queue_info.dim.ublock_rt * tt::tile_dim_to_array(input_queue_info.tile_dim)[0];
        epoch_tiles = input_count * op_info.output_dim.t * num_rows * (input_queue_info.dim.mblock_n/core_c_div);
        scatter_gather_num_tiles = num_rows;
    } else {
        int mblock_size_tiles = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct * op_info.output_dim.mblock_m * op_info.output_dim.mblock_n;
        epoch_tiles = input_count * op_info.output_dim.t * mblock_size_tiles;
        scatter_gather_num_tiles = mblock_size_tiles;
    }

    if (op_info.input_kernel_broadcasted(index)) {
      int t_factor = op_info.input_kernel_broadcasted_per_t(index) ? op_info.output_dim.t : 1;
      epoch_tiles = input_count * tile_clear_granularity * t_factor;
      scatter_gather_num_tiles = tile_clear_granularity;
    }

}


int Net2Pipe::get_op_kernel_input_size_tiles(tt_op_info op_info, int index, const temporal_epoch_context& epoch_context) const {
  
  bool embedding_op = (n2p::get_op_class(op_info) == OpClass::Embedding);
  bool tilizer_op = (n2p::get_op_class(op_info) == OpClass::Tilizer);
  bool tiles_no_header = (embedding_op || tilizer_op) && (index == 0);
  int tile_size_bytes = n2p::get_format_tile_size_bytes(op_info.input_data_formats[index], !tiles_no_header, op_info.input_tile_dims[index]);
  int kernel_clear_tiles = this->get_op_kernel_input_tile_clear_granularity(op_info, index);
  int kernel_clear_bytes = kernel_clear_tiles * tile_size_bytes;
  
  int min_size_bytes = 0;  
  auto incoming_bw_bytes_per_cycle = netlist_utils::is_valid_ethernet_op(op_info.type) ? 1/*ETH_BW_BYTES_PER_CYCLE*/ : NOC_BW_BYTES_PER_CYCLE;
  int min_size_bytes_full_latency_hiding = KERNEL_INPUT_MIN_LATENCY_CYCLES *  incoming_bw_bytes_per_cycle;

  // min_size_bytes_full_latency_hiding was picked so that it is 32KB, but because we want the number of tiles that fit into the buffer to be a power of 2
  // we need to force min_size_bytes to be divisible by tile_size_bytes, but also have min_size_bytes/tile_size_bytes be that nice power of 2 number
  // this code is a huristic for that
  if (min_size_bytes_full_latency_hiding % tile_size_bytes != 0) {
    int lower_pow2 = log2(tile_size_bytes);
    lower_pow2 = pow(2, lower_pow2);
    int higher_pow2 = lower_pow2 << 1;
    int sub1 = tile_size_bytes - lower_pow2;
    int sub2 = higher_pow2 - tile_size_bytes;
    int divisor = sub2 < sub1 ? higher_pow2 : lower_pow2;
    min_size_bytes_full_latency_hiding = (min_size_bytes_full_latency_hiding/divisor) * tile_size_bytes;
  }

  // FIXME imatosevic - revise this buffer sizing, for now only basic conditions to avoid excessive
  // buffers for many-input fused ops and input 0 of identity matmul
  if (op_info.input_kernel_broadcasted(index)) {
    min_size_bytes = 0;
  }
  else if (n2p::get_op_class(op_info) == OpClass::MatMul) {
    if (op_info.attributes.identity && ((index == 0) || (index == 2))) {
        min_size_bytes = 0;
    } else {
      min_size_bytes = min_size_bytes_full_latency_hiding;
    }
  }
  else if (n2p::get_op_class(op_info) == OpClass::Reduce) {
    min_size_bytes = min_size_bytes_full_latency_hiding;
  }
  else if ((n2p::get_op_class(op_info) == OpClass::FusedOp) ||
             (n2p::get_op_class(op_info) == OpClass::Nary)) {
    int num_op_inputs = op_info.input_names.size();
    min_size_bytes = min_size_bytes_full_latency_hiding;
    if (num_op_inputs > 3) {
      min_size_bytes = (min_size_bytes * 3) / num_op_inputs;
    }
    if (n2p::get_op_class(op_info) == OpClass::FusedOp) {
      string rel_input_name = "input" + to_string(index);
      const tt_scheduled_op_info &sub_op = epoch_context.fused_op_schedule_map.at(op_info.name).at(rel_input_name);

      if (sub_op.type == "matmul") {
        if (rel_input_name == sub_op.input_names.at(1)) {
            // For input1 of matmul sub_op we need whole macro block to be able to fit into input buffer.
            int matmul_input1_num_tiles = sub_op.m_k * sub_op.output_dim.mblock_n * kernel_clear_tiles;
            min_size_bytes = 2 * tile_size_bytes * matmul_input1_num_tiles;
        } else if (rel_input_name == sub_op.input_names.at(0) && sub_op.output_dim.mblock_n > 1) {
            // In case input1 is not a single column, we need to fit m_k * u_kt * ublock_rt into input0 buffer.
            int matmul_input0_num_tiles = sub_op.m_k * kernel_clear_tiles;
            min_size_bytes = 2 * tile_size_bytes * matmul_input0_num_tiles;
        }
      }
    }
  }
  else {
    min_size_bytes = min_size_bytes_full_latency_hiding;
  }

  if (min_size_bytes < (2*kernel_clear_bytes)) {
    min_size_bytes = 2*kernel_clear_bytes;
  }  

  int min_size_bytes_param = op_info.input_buf_min_size_tiles.at(index) * tile_size_bytes;
  int rounding_size_bytes;
  if (min_size_bytes < min_size_bytes_param) {
    min_size_bytes = min_size_bytes_param;
    rounding_size_bytes = kernel_clear_bytes;
  } else {
    // FIXME imatosevic - revise this
    // For now, unless minimal input buffer size is given in netlist, round up to 2*kernel_clear_bytes,
    // to keep perf results consistent with previous runs. 
    rounding_size_bytes = 2*kernel_clear_bytes;
  }
  
  if ((min_size_bytes % rounding_size_bytes) != 0) {
    min_size_bytes = ((min_size_bytes/rounding_size_bytes) + 1)*rounding_size_bytes;
  }
  
  return min_size_bytes/tile_size_bytes;
}

void n2p::get_op_input_mcast(const tt_op_info& op_info, int input_num,
                             const std::map<std::string, std::unordered_map<std::string, tt_scheduled_op_info>> &fused_op_schedule_map, 
                             bool& row_mcast, bool& col_mcast, bool& noc1_mcast) {

  bool invert_mcast_noc = false;
  if (n2p::get_op_class(op_info) == OpClass::MatMul) {
    
    if (op_info.attributes.identity) {
      if (input_num == 0) {
        row_mcast = false;
        col_mcast = false;
      } else if (input_num == 1) {
        row_mcast = false;
        col_mcast = true;
      } else if (input_num == 2) {
        row_mcast = false;
        col_mcast = false;
      } else {
        TT_ASSERT(op_info.attributes.bias, "Matmul identity op has 4th input but no bias attribute set");
        row_mcast = false;
        col_mcast = false;
      }
    } else { // matmul, identity = false
      if (input_num == 0) {        
        row_mcast = true;
        col_mcast = false;
      } else if (input_num == 1) {
        row_mcast = false;
        col_mcast = true;
      } else {
        TT_ASSERT(op_info.attributes.bias || op_info.attributes.requant || op_info.attributes.dequant, "Matmul op has more than 2 inputs but no bias or requant or dequant attribute set");
        row_mcast = false;
        col_mcast = false;
      }
    }
  } else if (n2p::get_op_class(op_info) == OpClass::Depthwise) {
    if (input_num == 0) {
        row_mcast = true;
        col_mcast = false;
    } else if (input_num == 1) {
        row_mcast = false;
        col_mcast = true;
    } else {
        TT_ASSERT(op_info.attributes.bias, "Depthwise op has 3rd input but no bias attribute set");
        row_mcast = false;
        col_mcast = false;
    }
  } else if (n2p::get_op_class(op_info) == OpClass::Reduce) {
    row_mcast = false;
    col_mcast = false;
  } else if (n2p::get_op_class(op_info) == OpClass::FusedOp) {
    string rel_input_name = "input" + to_string(input_num); 
    std::unordered_map<std::string, tt_scheduled_op_info> input2op_map = fused_op_schedule_map.at(op_info.name);
    int num_row_mcasts = 0;
    int num_col_mcasts = 0;
    row_mcast = false;
    col_mcast = false;
    for (int i = 0; i < op_info.input_names.size(); i++) {
        std::string input_name_w_index = "input" + std::to_string(i);
        if (input2op_map[input_name_w_index].type == "matmul") {
          if (input_name_w_index == input2op_map[input_name_w_index].input_names.at(0)) {
            if (input_num == i) {
                row_mcast = true;
                if (num_row_mcasts > 0) {                
                    invert_mcast_noc = true;                
                }
            }
            num_row_mcasts++;
          } else {
            if (input_num == i) {
                col_mcast = true;
                if (num_col_mcasts > 0) {                
                    invert_mcast_noc = true;                
                }
            }
            num_col_mcasts++;
          }
        }
    }
    int total_mcasts = 0;
    if (op_info.grid_size_logical_c() > 1) {
        total_mcasts += num_row_mcasts;
    }
    if (op_info.grid_size_logical_r() > 1) {
        total_mcasts += num_col_mcasts;
    }
    if (total_mcasts > 2) {
        ERROR(std::string("Fused op ") + op_info.name + " has more than two multicast inputs");
    }
  } else if (n2p::get_op_class(op_info) == OpClass::Nary) {
    row_mcast = false;
    col_mcast = false;
  } else if (n2p::get_op_class(op_info) == OpClass::Buffer) {
    if (input_num == 0) {
      row_mcast = false;
      col_mcast = true;
    } else if (input_num == 1) {
      row_mcast = true;
      col_mcast = false;
    }
  } else if (n2p::get_op_class(op_info) == OpClass::Embedding || n2p::get_op_class(op_info) == OpClass::Tilizer) {
    if (input_num == 0) {
      row_mcast = false;
      col_mcast = false;
    } else {
      row_mcast = true;
      col_mcast = false;
    }
  } else { // eltwise op
    row_mcast = false;
    col_mcast = false;
  }

  TT_ASSERT(!(row_mcast && col_mcast), "net2pipe error: op " + op_info.name + ", input = " + std::to_string(input_num) + " has both row and column multicast");

  // FIXME imatosevic - this info should move to router
  // We always use NOC1 for y-multicast and NOC0 for x-multicast.
  // grid_transpose flag determines how this maps to the op logical row/column directions
  noc1_mcast = op_info.grid_transpose ? row_mcast : col_mcast;
  if (invert_mcast_noc) {
    noc1_mcast = !noc1_mcast;
  }
}

int n2p::get_op_input_total_bcast_factor(tt_op_info op_info, int index) {
  int result = 1;
  for (auto it : op_info.input_tm_ops[index]) {
    string tm_name = get<0>(it);
    std::vector<int> curr_tm_args = get<1>(it);
    if (tm_name == "c_broadcast" || tm_name == "r_broadcast" || tm_name == "z_broadcast") {
      result *= curr_tm_args[0];
    }
  }
  return result;
}


int Net2Pipe::get_op_kernel_input_tile_clear_granularity(tt_op_info op_info, int index) const {
    int result;

    const unordered_map<string, tt_fused_op_info> &fused_ops_map = this->parsed_netlist.fused_ops_op_map;
    
    if (op_info.input_kernel_broadcasted(index)) {
      result = op_info.input_kernel_broadcasted(index);
    }
    else if (n2p::get_op_class(op_info) == OpClass::MatMul) {
        if (op_info.attributes.identity) {
            if (index == 0) {
                result = op_info.attributes.num_sparse_tiles;
            } else if (index == 1) {
                result = op_info.output_dim.mblock_n * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
            } else if (index == 2) {
                result = 1; 
            } else {
               if (!op_info.attributes.bias)  {
                  ERROR(std::string("Op ") + op_info.name + " has 4th input but no bias attribute set ");
               }
               result = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
            }
        } else {
            if (index == 0) {
                result = (op_info.attributes.min_input_buffer[0] ? 1 : op_info.output_dim.mblock_m) * op_info.output_dim.ublock_rt * op_info.attributes.u_kt;
            } else if (index == 1) {
                result = (op_info.attributes.min_input_buffer[1] ? 1 : op_info.output_dim.mblock_n) * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
            } else  {
               if (!(op_info.attributes.bias || op_info.attributes.requant || op_info.attributes.dequant))  {
                  ERROR(std::string("Op ") + op_info.name + " has more than 2 inputs but no bias or requant or dequant attribute set ");
               }
               result = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
            }
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Depthwise) {
        if (index == 0) {
            result =  (op_info.attributes.min_input_buffer[0] ? 1 : op_info.output_dim.mblock_m) * op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
        } else if (index == 1) {
            result =  (op_info.attributes.min_input_buffer[1] ? 1 : op_info.output_dim.mblock_n) * op_info.output_dim.ublock_ct * op_info.attributes.u_kt; // u_kt == 1 for depthwise
        } else  {
            if (!op_info.attributes.bias)  {
                ERROR(std::string("Op ") + op_info.name + " has 3rd input but no bias attribute set ");
            }
            result = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Reduce) {
        if (op_info.attributes.reduce_dim == Dim::R) {
           result = op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
        } else if (op_info.attributes.reduce_dim == Dim::C) {
           result = op_info.output_dim.ublock_rt * op_info.attributes.u_kt;
        } else if (op_info.attributes.reduce_dim == Dim::Z) {
           result = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
        } else {
           TT_ASSERT(false, "unsupported reduce dim. only r, c and z are supported");
        }
    } else if (n2p::get_op_class(op_info) == OpClass::FusedOp) {
        string rel_input_name = "input" + to_string(index); //op_info.fused_op_info.input_names_to_rel_names_map[op_info.input_names.at(index)];
        std::unordered_map<std::string, tt_scheduled_op_info> input2op_map = netlist_utils::get_map_of_consumer_ops_per_input(op_info, fused_ops_map);

        //int mblock_m = input2op_map[rel_input_name].output_dim.mblock_m;
        //int mblock_n = input2op_map[rel_input_name].output_dim.mblock_n;

        int ublock_rt = input2op_map[rel_input_name].output_dim.ublock_rt;
        int ublock_ct = input2op_map[rel_input_name].output_dim.ublock_ct;

        if (input2op_map[rel_input_name].type == "matmul") {
            int u_kt = input2op_map[rel_input_name].u_kt; 
            int m_k  = input2op_map[rel_input_name].m_k; 
            if (rel_input_name == input2op_map[rel_input_name].input_names.at(0)) {
                result = ublock_rt * u_kt;
            } else {
                result = u_kt * ublock_ct;
            }
        } else if (input2op_map[rel_input_name].type == "reduce") {
            int u_kt = input2op_map[rel_input_name].u_kt; 
            if (input2op_map[rel_input_name].reduce_dim == Dim::R) {
                result = ublock_ct * u_kt;
            } else if (input2op_map[rel_input_name].reduce_dim == Dim::C) {
                result = ublock_rt * u_kt;
            } else {
                TT_ASSERT(false, "unsupported reduce dim in fused op. only r and c are supported");
            }    
        } else {
            result = ublock_rt * ublock_ct;
        }    

        if (op_info.fused_op_info.forked_input_names.find(rel_input_name) != op_info.fused_op_info.forked_input_names.end()) {
            result *= op_info.fused_op_info.forked_input_names[rel_input_name]; // scale buffer size to support input forking
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Buffer) {
        if (index == 0) {
            result = (op_info.attributes.min_input_buffer[0] ? 1 : op_info.output_dim.mblock_n) * op_info.output_dim.ublock_ct * op_info.attributes.u_kt;
        } else  {
            result = 1;
        } 
    } else if (n2p::get_op_class(op_info) == OpClass::Embedding) {
        if (index == 0) {
            std::string table_input_name = op_info.input_names[index];
            tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
            result = tt::tile_dim_to_array(input_queue_info.tile_dim)[0]; //kernel always consumes one row of input ublock, pipegen needs this in row tiles
        } else {
            result = 1; // cleared by firmware
        }
    } else if (n2p::get_op_class(op_info) == OpClass::Tilizer) {
        std::string table_input_name = op_info.input_names[index];
        tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
        result = input_queue_info.dim.ublock_rt * tt::tile_dim_to_array(input_queue_info.tile_dim)[0]; //kernel always consumes one row of input ublock, pipegen needs this in row tiles
    } else {
        result = op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct;
    }
    
    return result;
}

void Net2Pipe::get_op_output(
    const tt_op_info& op_info,
    int input_count,
    const temporal_epoch_context& epoch_context,
    int core_r,
    int core_c,
    int &id,
    int &is_scatter,
    int &replicate,
    int &tile_size_bytes,
    int &epoch_tiles,
    int &size_tiles,
    int &scatter_gather_num_tiles,
    int &tiles_per_input) const {

    // FIXME imatosevic - which other data format checks do we need? 
    if ((op_info.dest_accumulate_data_format == tt::DataFormat::Float32) &&
        (this->config.arch == tt::ARCH::GRAYSKULL)) {
      ERROR(std::string("Op ") + op_info.name + " has dest accumulate data format incompatible with arch = " + get_arch_str(this->config.arch));
    }

    int output_full_buf_size_mblocks = n2p::get_op_output_full_buf_size_mblocks(op_info);  // includes double-buffering
    int output_single_buf_size_mblocks = n2p::get_op_output_single_buf_size_mblocks(op_info);  // does not include double-buffering
    int mblock_tiles = n2p::get_op_output_mblock_tiles(op_info);
    tiles_per_input = n2p::get_op_output_tiles_per_input(op_info);
    tile_size_bytes = n2p::get_format_tile_size_bytes(op_info.output_data_format, !op_info.untilize_output, op_info.output_tile_dim);
    id = 16;

    // TODO:
    // Truncate the requested buffer allocation if the total number of mblocks in the epoch is less than half of the allocated buffer
    // Maybe this should actually be if it is less than the full buffer allocation...
    bool truncate_buffer = input_count * op_info.output_dim.t <= output_single_buf_size_mblocks;

    // For buffer op there is no dependancy between output buffer size and t
    // Consumer of the buffer op has no TMs on the pipe
    if (n2p::get_op_class(op_info) != OpClass::Buffer) {
        if (!divisible_either_direction(output_single_buf_size_mblocks, op_info.output_dim.t)) {
          ERROR(std::string("Op ") + op_info.name + " has t = " + std::to_string(op_info.output_dim.t) + " and stores " + std::to_string(output_single_buf_size_mblocks) + " mblocks on output, numbers not divisible either way");
        }
    }    

    // FIXME imatosevic - check if we still need any constraint on total input count t's? 
    // if (((input_count * op_info.output_dim.t) % output_single_buf_size_mblocks) != 0) {
    //   ERROR(std::string("Op ") + op_info.name + " with t = " + std::to_string(op_info.output_dim.t) + " and input count = " + std::to_string(input_count) + " double-buffers a non-divisible number of mblocks on output (" + std::to_string(output_single_buf_size_mblocks) + ")");
    // }
    
    if (is_output_scatter(op_info.name, scatter_gather_num_tiles, epoch_context)) {
        is_scatter = 1;
        TT_ASSERT(n2p::get_op_class(op_info) != OpClass::Buffer); // Buffer op output must not be scatter as there is no TM between buffer op output and consumer input
        // replicate * size_tiles = total output buffer size as observed by packer
        if (truncate_buffer) {
            replicate = (input_count * op_info.output_dim.t * mblock_tiles) / scatter_gather_num_tiles;
            output_single_buf_size_mblocks = input_count * op_info.output_dim.t;
        }
        else {
            replicate = (output_single_buf_size_mblocks * mblock_tiles) / scatter_gather_num_tiles;
        }

        if (truncate_buffer) {
          size_tiles = (output_single_buf_size_mblocks * mblock_tiles) / replicate;  // force single-buffering if single input in ubatch
        } else {
          size_tiles = (output_full_buf_size_mblocks * mblock_tiles) / replicate;
        }
        n2p::Log() << "output_single_buf_size_mblocks: " << output_single_buf_size_mblocks << endl;
        n2p::Log() << "scatter_gather_num_tiles: " << scatter_gather_num_tiles << endl;
        n2p::Log() << "replicate: " << replicate << endl;
        n2p::Log() << "input_count: " << input_count << endl;
        n2p::Log() << "op_info.output_dim.t: " << op_info.output_dim.t << endl;
        n2p::Log() << "mblock_tiles: " << mblock_tiles << endl;
        epoch_tiles = (input_count * op_info.output_dim.t * mblock_tiles) / replicate;
        TT_ASSERT(epoch_tiles > 0);
    } else {
        is_scatter = 0;
        replicate = 0;
        scatter_gather_num_tiles = mblock_tiles * ((n2p::get_op_class(op_info) == OpClass::Buffer) ? 1 : output_single_buf_size_mblocks); // Buffer op scatter granularity is always mblock_tiles (strip)
        size_tiles = op_info.untilize_output
                         ? (output_full_buf_size_mblocks) * (op_info.output_dim.ublock_rt * op_info.output_dim.ublock_ct * op_info.output_dim.mblock_n)
                         : output_full_buf_size_mblocks * mblock_tiles;

        epoch_tiles = input_count * ((n2p::get_op_class(op_info) == OpClass::Buffer) ? op_info.attributes.num_nz_strips[core_r*op_info.grid_size_x()+core_c] : op_info.output_dim.t) * mblock_tiles;
        TT_ASSERT(epoch_tiles > 0);
    }
}

void Net2Pipe::compute_queue_tms(std::string queue_name, int input_count, temporal_epoch_context& epoch_context) const {
    
    tt_queue_info queue_info = this->parsed_netlist.queue_map.at(queue_name);
    if (!netlist_parser::is_queue_fed_by_op(queue_info)) {
        return;
    }

    tt_op_info input_op_info = epoch_context.op_info_map.at(queue_info.input);

    int producer_buf_size = std::min(input_count * input_op_info.output_dim.t, n2p::get_op_output_single_buf_size_mblocks(input_op_info));
    int producer_output_t_buf = n2p::get_op_class(input_op_info) == OpClass::Buffer ? 1 : producer_buf_size; // Override buf size mb as there is no TM on buffer op output
    three_d_array_tile_src_map input_src_tm(
            queue_info.name,
            input_op_info.name,
            input_op_info.output_dim.t,
            input_op_info.output_dim.ublock_rt,
            input_op_info.output_dim.ublock_ct,
            input_op_info.output_dim.mblock_m,
            input_op_info.output_dim.mblock_n,
            input_op_info.grid_size_logical_r(),
            input_op_info.grid_size_logical_c(),
            producer_output_t_buf,
            producer_output_row_major_ublock_scan_order(input_op_info.name, epoch_context));

    if (input_op_info.untilize_output) {
        // FIXME imatosevic - presently we allow both formats for untilized queues; should we enforce stricter check (grid, ublock=1x1)?
        if (((queue_info.grid_size.r*queue_info.dim.mblock_m*queue_info.dim.ublock_rt) != (input_op_info.grid_size_logical_r() * input_op_info.output_dim.mblock_m * input_op_info.output_dim.ublock_rt)) ||
            ((queue_info.grid_size.c*queue_info.dim.mblock_n*queue_info.dim.ublock_ct) != (input_op_info.grid_size_logical_c() * input_op_info.output_dim.mblock_n * input_op_info.output_dim.ublock_ct)) ||
            (queue_info.dim.t != input_op_info.output_dim.t)) {
          ERROR(std::string("Queue ") + queue_name + " has incompatible block/grid dimensions with untilized input op: " + queue_info.input);
        }
        if (queue_info.input_tm_ops.size() > 0) {
          ERROR(std::string("Queue ") + queue_name + " has TMs on untilized input");
        }
    }

    if (input_op_info.gradient_op) {
        // For gradient ops, we don't generate output pipes in pipegen.yaml.
        // Rather, the intermediate buffer is tagged with DRAM flags and address.
        if ((input_op_info.grid_size_logical_r() != queue_info.grid_size.r) ||
            (input_op_info.grid_size_logical_c() != queue_info.grid_size.c) ||
            (input_op_info.output_dim.mblock_m != queue_info.dim.mblock_m) ||
            (input_op_info.output_dim.mblock_n != queue_info.dim.mblock_n) ||
            (input_op_info.output_dim.ublock_rt != queue_info.dim.ublock_rt) ||
            (input_op_info.output_dim.ublock_ct != queue_info.dim.ublock_ct) ||
            (input_op_info.output_dim.t != queue_info.dim.t)) {
            ERROR(std::string("Queue ") + queue_name + " has incompatible block/grid dimensions with gradient input op: " + queue_info.input);
        }
         if (queue_info.input_tm_ops.size() > 0) {
          ERROR(std::string("Queue ") + queue_name + " has TMs with gradient input op: " + queue_info.input);
        }
    }

    // Apply TMs
    for (auto it : queue_info.input_tm_ops[0]) {
        string tm_name = get<0>(it);
        std::vector<int> curr_tm_args = get<1>(it);
        input_src_tm = input_src_tm.apply_tm(tm_name, curr_tm_args);
    }

    bool queue_row_major_scan_order = (queue_info.dim.ublock_order == Dim::R);

    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
        0, // kernel_bcast_tiles
        false, // kernel_bcast_tiles_per_t
        queue_info.dim.t,
        queue_info.dim.ublock_rt,
        queue_info.dim.ublock_ct,
        queue_info.dim.mblock_m,
        queue_info.dim.mblock_n,
        queue_info.grid_size.r,
        queue_info.grid_size.c,
        queue_row_major_scan_order);

    // check restrictions on queue input pipes
    std::set<std::pair<int, int>> dest_cores;
    for(auto & pipe_it : input_tm.pipes) {
        phase_pipe_tile_map pipe = pipe_it.second;
        if (pipe.dest_cores.size() > 1) {
            ERROR(std::string("Queue ") + queue_name + " has input stacking pipe with multiple destination buffers");
        }
        std::pair<int, int> dest_core = pipe.dest_cores[0];
        if (dest_cores.find(dest_core) != dest_cores.end()) {
            ERROR(std::string("Queue ") + queue_name + " has multiple input pipes to buffer at grid r=" + std::to_string(dest_core.first) + ", c=" + std::to_string(dest_core.second));
        }
        dest_cores.insert(dest_core);
        bool first_input = true;
        int first_producer_core_r = -1;
        int first_producer_core_c = -1;
        for (tile_to_core_index_map core_tile_index : pipe.tile_map) {
            if (first_input) {
                first_producer_core_r = core_tile_index.core_r;
                first_producer_core_c = core_tile_index.core_c;
                first_input = false;
            } else {
                if ((core_tile_index.core_r != first_producer_core_r || core_tile_index.core_c != first_producer_core_c) && !input_op_info.untilize_output) {
                    ERROR(std::string("Queue ") + queue_name + " has input pipe with multiple producer cores for buffer at grid r=" + std::to_string(dest_core.first) + ", c=" + std::to_string(dest_core.second));
                }
            }
        }
    }

    epoch_context.queue_input_tm_pipes_map[queue_name] = input_tm;
}



void Net2Pipe::compute_op_tms(std::string op_name, int input_count, temporal_epoch_context& epoch_context) const {
    tt_op_info op_info = epoch_context.op_info_map.at(op_name);
    int num_op_inputs = op_info.input_names.size();

    for (int input_num = 0; input_num < num_op_inputs; input_num++) {
        std::string input_name = op_info.input_names[input_num];

        int in_t;
        int in_ublock_r, in_ublock_c;
        int in_mblock_m, in_mblock_n;
        int in_cores_r, in_cores_c;
        int producer_output_t_buf;
        if (name_is_queue(input_name)) {
            tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(input_name);
            in_t = input_queue_info.dim.t;
            producer_output_t_buf = in_t;
            in_ublock_r = input_queue_info.dim.ublock_rt;
            in_ublock_c = input_queue_info.dim.ublock_ct;
            in_mblock_m = input_queue_info.dim.mblock_m;
            in_mblock_n = input_queue_info.dim.mblock_n;
            in_cores_r = input_queue_info.grid_size.r;
            in_cores_c = input_queue_info.grid_size.c;

        } else if (name_is_op(input_name, epoch_context)) {
            tt_op_info input_op_info = epoch_context.op_info_map.at(input_name);
            in_t = input_op_info.output_dim.t;
            int producer_buf_size = std::min(input_count * input_op_info.output_dim.t, n2p::get_op_output_single_buf_size_mblocks(input_op_info));
            producer_output_t_buf = n2p::get_op_class(input_op_info) == OpClass::Buffer ? 1 : producer_buf_size; // Override buf size mb as there is no TM on buffer op output
            in_ublock_r = input_op_info.output_dim.ublock_rt;
            in_ublock_c = input_op_info.output_dim.ublock_ct;
            in_mblock_m = input_op_info.output_dim.mblock_m;
            in_mblock_n = input_op_info.output_dim.mblock_n;
            in_cores_r = input_op_info.grid_size_logical_r();
            in_cores_c = input_op_info.grid_size_logical_c();
        } else {
            ERROR(std::string("Op ") + op_name + " - unknown input name: " + input_name);
        }

        three_d_array_tile_src_map input_src_tm(
            input_name,
            op_name,
            in_t,
            in_ublock_r,
            in_ublock_c,
            in_mblock_m,
            in_mblock_n,
            in_cores_r,
            in_cores_c,
            producer_output_t_buf,
            producer_output_row_major_ublock_scan_order(input_name, epoch_context));

        // Unpadding
        input_src_tm = input_src_tm.unpad(op_info.input_unpadding.at(input_num).rt, op_info.input_unpadding.at(input_num).ct);

        // Apply TMs
        for (auto it : op_info.input_tm_ops[input_num]) {
            string tm_name = get<0>(it);
            std::vector<int> curr_tm_args = get<1>(it);
            input_src_tm = input_src_tm.apply_tm(tm_name, curr_tm_args);
        }
        
        // Apply Padding
        input_src_tm = input_src_tm.pad(op_info.input_padding.at(input_num).rt, op_info.input_padding.at(input_num).ct);

        // Verification / Assertions for post padding
        bool tm_padding = false;
        for (auto it : op_info.input_tm_ops.at(input_num)) {
            const string &tm_name = get<0>(it);
            TT_ASSERT(not tm_padding or tm_padding and (tm_name == "hslice" or tm_name == "vslice"), "Padding must only be followed by slice: tm: {}", tm_name);
            tm_padding |= tm_name == "pad";
        }

        const int kernel_bcast_tiles = op_info.input_kernel_broadcasted(input_num);
        const int kernel_bcast_tiles_per_t = op_info.input_kernel_broadcasted_per_t(input_num);

        if (n2p::get_op_class(op_info) == OpClass::MatMul) {
            if (op_info.attributes.identity) {
                if (input_num == 0) {
                    bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);
                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        1,
                        1,
                        1,
                        1,
                        op_info.attributes.num_sparse_tiles,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c(),
                        op_output_row_major_scan_order);

                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                    if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                        int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                        int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                        epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                        log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                         op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                         input_num, input_name, max_num_tiles, kernel_buf_size);
                    }
                } else if (input_num == 1) {
                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_col_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        input_src_tm.get_size(map_dims::t),
                        op_info.attributes.u_kt,
                        op_info.output_dim.ublock_ct,
                        op_info.attributes.m_k,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c());

                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                } else if (input_num == 2) {
                    bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);
                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        1,
                        1,
                        1,
                        1,
                        op_info.attributes.num_index_tiles,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c(),
                        op_output_row_major_scan_order);
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                    if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                        int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                        int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                        epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                        log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                         op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                         input_num, input_name, max_num_tiles, kernel_buf_size);
                    }

                } else {
                    TT_ASSERT(op_info.attributes.bias, "Matmul identity op has 4th input but no bias attribute set");

                    bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);

                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        op_info.output_dim.t,
                        op_info.output_dim.ublock_rt,
                        op_info.output_dim.ublock_ct,
                        op_info.output_dim.mblock_m,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c(),
                        op_output_row_major_scan_order);
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;

                    if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                        int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                        int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                        epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                        log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                            op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                            input_num, input_name, max_num_tiles, kernel_buf_size);
                    }
                }

            } else {
                int t = op_info.output_dim.t * (op_info.attributes.accumulate ? op_info.attributes.z : 1);
                if (input_num == 0) {


                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_row_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        t,
                        op_info.output_dim.ublock_rt,
                        op_info.attributes.u_kt,
                        op_info.output_dim.mblock_m,
                        op_info.attributes.m_k,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c());

                    // TODO: row prolog intermediate scheme if-need-be

                    const int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                    const int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                    const bool unroll_and_post_tm_prolog = is_name_prolog_queue(op_info.input_names[input_num], epoch_context) and
                                                           max_num_tiles <= kernel_buf_size;

                    log_debug(tt::LogNet2Pipe, "matmul in0 unroll prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                        op_name, unroll_and_post_tm_prolog,
                        input_num, input_name, max_num_tiles, kernel_buf_size);

                    if(unroll_and_post_tm_prolog) {
                        n2p::unroll_pipes(input_tm);
                        epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                        epoch_context.prolog_post_tm_operand[op_name][input_name] = true;
                    }
                    else {
                        epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                    }

                } else if (input_num == 1) {
                    
                    auto prolog_layout = n2p::get_prolog_layout(
                            t,
                            op_info.attributes.u_kt,
                            op_info.attributes.m_k,
                            op_info.output_dim.ublock_ct,
                            op_info.output_dim.mblock_n,
                            op_info.grid_size[0],
                            op_info.grid_size[1]
                        );
                    /*const bool prolog_will_reblock = prolog_layout.num_chunks_to_gather != 1 and 
                                                (prolog_layout.num_chunks_to_gather != input_src_tm.producer_data_format.num_cores_r or
                                                prolog_layout.num_cores_to_mcast != input_src_tm.producer_data_format.num_cores_c);
*/
                    const bool prolog_will_reblock = true;
                    
                    const bool do_two_step_matmul_prolog =   prolog_will_reblock and 
                                                             op_info.input_tm_ops[input_num].size() == 0 and
                                                             is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context);
                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_col_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        t,
                        op_info.attributes.u_kt,
                        op_info.output_dim.ublock_ct,
                        op_info.attributes.m_k,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size[0],
                        op_info.grid_size[1]);

                    int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                    
                    const bool unroll_and_post_tm_prolog = is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context) and
                                                           max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                    
                    //log_debug(tt::LogNet2Pipe, "unroll_and_post_tm_prolog: '{}', max_num_tiles: '{}'; get_op_kernel_input_size_tiles(op_info, input_num): {}",
                    //    unroll_and_post_tm_prolog, max_num_tiles, get_op_kernel_input_size_tiles(op_info, input_num));

                    if(unroll_and_post_tm_prolog) {
                        n2p::unroll_pipes(input_tm);
                        epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                        epoch_context.prolog_post_tm_operand[op_name][input_name] = true;
                    }
                    else if (do_two_step_matmul_prolog) { 
                        // prolog
                        epoch_context.prolog_layout_per_op[op_name][input_name] = n2p::get_prolog_layout(
                            t,
                            op_info.attributes.u_kt,
                            op_info.attributes.m_k,
                            op_info.output_dim.ublock_ct,
                            op_info.output_dim.mblock_n,
                            op_info.grid_size[0],
                            op_info.grid_size[1]
                        );

                        consumer_to_producer_tile_map prolog_tm;
                        consumer_to_producer_tile_map gather_scatter_tm;

                        std::tie(prolog_tm, gather_scatter_tm) = input_src_tm.get_prolog_matmul_col_input(
                            kernel_bcast_tiles,
                            kernel_bcast_tiles_per_t,
                            t,
                            op_info.attributes.u_kt,
                            op_info.attributes.m_k,
                            op_info.output_dim.ublock_ct,
                            op_info.output_dim.mblock_n,
                            op_info.grid_size[0],
                            op_info.grid_size[1]);

                        // Save tile map data for pipe creation later
                        epoch_context.prolog_tm_pipes_map[op_name][input_name] = prolog_tm;
                        epoch_context.op_input_tm_pipes_map[op_name][input_name] = prolog_tm;
                        epoch_context.prolog_input_pipes_map[op_name][input_name] = gather_scatter_tm;

                        epoch_context.prolog_post_tm_operand[op_name][input_name] = true;
                    }
                    else { // Default tile / pipe map
                        epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                    }
                } else {
                    TT_ASSERT(op_info.attributes.bias || op_info.attributes.requant || op_info.attributes.dequant, "Matmul op has more than 2 inputs but no bias or requant or dequant attribute set");

                    bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);

                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        op_info.output_dim.t,
                        op_info.output_dim.ublock_rt,
                        op_info.output_dim.ublock_ct,
                        op_info.output_dim.mblock_m,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c(),
                        op_output_row_major_scan_order);
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;

                    if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                        int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                        int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                        epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                        log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                            op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                            input_num, input_name, max_num_tiles, kernel_buf_size);
                    }
                }
            }    
        } else if (n2p::get_op_class(op_info) == OpClass::Depthwise) {
            int t = op_info.output_dim.t;
            if (input_num == 0) {
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_row_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    t,
                    op_info.output_dim.ublock_rt,
                    op_info.output_dim.ublock_ct,
                    op_info.output_dim.mblock_m,
                    op_info.attributes.m_k * op_info.output_dim.mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c());

                // TODO: row prolog intermediate scheme if-need-be

                const int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                const int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                const bool unroll_and_post_tm_prolog = is_name_prolog_queue(op_info.input_names[input_num], epoch_context) and
                                                        max_num_tiles <= kernel_buf_size;

                log_debug(tt::LogNet2Pipe, "matmul in0 unroll prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                    op_name, unroll_and_post_tm_prolog,
                    input_num, input_name, max_num_tiles, kernel_buf_size);

                if(unroll_and_post_tm_prolog) {
                    n2p::unroll_pipes(input_tm);
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                    epoch_context.prolog_post_tm_operand[op_name][input_name] = true;
                }
                else {
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                }
            } else if (input_num == 1) {
                auto prolog_layout = n2p::get_prolog_layout(
                        t,
                        op_info.attributes.u_kt, // u_kt == 1 for depthwise
                        op_info.attributes.m_k,
                        op_info.output_dim.ublock_ct,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size[0],
                        op_info.grid_size[1]
                    );

                const bool do_two_step_matmul_prolog =  op_info.input_tm_ops[input_num].size() == 0 and
                                                        is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context);
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_col_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    t,
                    op_info.attributes.u_kt, // u_kt == 1 for depthwise
                    op_info.output_dim.ublock_ct,
                    op_info.attributes.m_k,
                    op_info.output_dim.mblock_n,
                    op_info.grid_size[0],
                    op_info.grid_size[1]);

                int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);

                const bool unroll_and_post_tm_prolog = is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context) and
                                                        max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);

                //log_debug(tt::LogNet2Pipe, "unroll_and_post_tm_prolog: '{}', max_num_tiles: '{}'; get_op_kernel_input_size_tiles(op_info, input_num): {}",
                //    unroll_and_post_tm_prolog, max_num_tiles, get_op_kernel_input_size_tiles(op_info, input_num));

                if(unroll_and_post_tm_prolog) {
                    n2p::unroll_pipes(input_tm);
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                    epoch_context.prolog_post_tm_operand[op_name][input_name] = true;
                }
                else if (do_two_step_matmul_prolog) { 
                    // prolog
                    epoch_context.prolog_layout_per_op[op_name][input_name] = n2p::get_prolog_layout(
                        t,
                        op_info.attributes.u_kt, // u_kt == 1 for depthwise
                        op_info.attributes.m_k,
                        op_info.output_dim.ublock_ct,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size[0],
                        op_info.grid_size[1]
                    );

                    consumer_to_producer_tile_map prolog_tm;
                    consumer_to_producer_tile_map gather_scatter_tm;

                    std::tie(prolog_tm, gather_scatter_tm) = input_src_tm.get_prolog_matmul_col_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        t,
                        op_info.attributes.u_kt, // u_kt == 1 for depthwise
                        op_info.attributes.m_k,
                        op_info.output_dim.ublock_ct,
                        op_info.output_dim.mblock_n,
                        op_info.grid_size[0],
                        op_info.grid_size[1]);

                    // Save tile map data for pipe creation later
                    epoch_context.prolog_tm_pipes_map[op_name][input_name] = prolog_tm;
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = prolog_tm;
                    epoch_context.prolog_input_pipes_map[op_name][input_name] = gather_scatter_tm;

                    epoch_context.prolog_post_tm_operand[op_name][input_name] = true;
                }
                else { // Default tile / pipe map
                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                }
            } else {
                TT_ASSERT(op_info.attributes.bias, "Matmul op has 3rd input but no bias attribute set");

                bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);

                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    op_info.output_dim.t,
                    op_info.output_dim.ublock_rt,
                    op_info.output_dim.ublock_ct,
                    op_info.output_dim.mblock_m,
                    op_info.output_dim.mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c(),
                    op_output_row_major_scan_order);
                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;

                if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                    int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                    int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                    epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                    log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                        op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                        input_num, input_name, max_num_tiles, kernel_buf_size);
                }
            }
        } else if (n2p::get_op_class(op_info) == OpClass::Reduce) {
            int mblock_r = 0;
            int mblock_c = 0;
            int ublock_rt = 0;
            int ublock_ct = 0;
            bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);
            int t = op_info.output_dim.t;

            if (op_info.attributes.reduce_dim == Dim::R) {
                mblock_r = op_info.attributes.m_k;
                mblock_c = op_info.output_dim.mblock_n;

                ublock_rt = op_info.attributes.u_kt;
                ublock_ct = op_info.output_dim.ublock_ct;

		        op_output_row_major_scan_order = false;

            } else if (op_info.attributes.reduce_dim == Dim::C) {
                mblock_r = op_info.output_dim.mblock_m;
                mblock_c = op_info.attributes.m_k;

                ublock_rt = op_info.output_dim.ublock_rt;
                ublock_ct = op_info.attributes.u_kt;

		        op_output_row_major_scan_order = true;
            } else if (op_info.attributes.reduce_dim == Dim::Z) {
                mblock_r = op_info.output_dim.mblock_m;
                mblock_c = op_info.output_dim.mblock_n;

                ublock_rt = op_info.output_dim.ublock_rt;
                ublock_ct = op_info.output_dim.ublock_ct;

		        op_output_row_major_scan_order = true;

                t = op_info.output_dim.t*op_info.attributes.z;

            } else {
                TT_ASSERT(false, "unsupported reduce dim. only r, c and z are supported");
            }

            consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                kernel_bcast_tiles,
                kernel_bcast_tiles_per_t,
                t,
                ublock_rt,
                ublock_ct,
                mblock_r,
                mblock_c,
                op_info.grid_size_logical_r(),
                op_info.grid_size_logical_c(),
                op_output_row_major_scan_order);
            epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;

        } else if (n2p::get_op_class(op_info) == OpClass::FusedOp) {
            string rel_input_name = "input" + to_string(input_num); //op_info.fused_op_info.input_names_to_rel_names_map[op_info.input_names.at(input_num)];
            std::unordered_map<std::string, tt_scheduled_op_info> input2op_map = epoch_context.fused_op_schedule_map[op_info.name];

            bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context); //FIXME: acejkov check if this can be global

            int mblock_m = input2op_map[rel_input_name].output_dim.mblock_m;
            int mblock_n = input2op_map[rel_input_name].output_dim.mblock_n;

            int ublock_rt = input2op_map[rel_input_name].output_dim.ublock_rt;
            int ublock_ct = input2op_map[rel_input_name].output_dim.ublock_ct;


            if (input2op_map[rel_input_name].type == "matmul") {
                int m_k = input2op_map[rel_input_name].m_k; 
                int u_kt = input2op_map[rel_input_name].u_kt; 
                if (rel_input_name == input2op_map[rel_input_name].input_names.at(0)) {
                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_row_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        op_info.output_dim.t,
                        ublock_rt,
                        u_kt,
                        mblock_m,
                        m_k,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c(), 
                        true);

                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;

               } else {
                    consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_col_input(
                        kernel_bcast_tiles,
                        kernel_bcast_tiles_per_t,
                        op_info.output_dim.t,
                        u_kt,
                        ublock_ct,
                        m_k,
                        mblock_n,
                        op_info.grid_size_logical_r(),
                        op_info.grid_size_logical_c(),
                        false);

                    epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
               } 
            } else if (input2op_map[rel_input_name].type == "reduce")  {
                if (input2op_map[rel_input_name].reduce_dim == Dim::R) {
                    mblock_m = input2op_map[rel_input_name].m_k;
                    ublock_rt = input2op_map[rel_input_name].u_kt;
		            op_output_row_major_scan_order = false;
                } else if (input2op_map[rel_input_name].reduce_dim == Dim::C) {
                    mblock_n = input2op_map[rel_input_name].m_k; 
                    ublock_ct = input2op_map[rel_input_name].u_kt;
		            op_output_row_major_scan_order = true;
                } else {
                    TT_ASSERT(false, "unsupported reduce dim in fused op. only r and c are supported");
                }
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    op_info.output_dim.t,
                    ublock_rt,
                    ublock_ct,
                    mblock_m,
                    mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c(),
                    op_output_row_major_scan_order);
                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            } else {
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    op_info.output_dim.t,
                    ublock_rt,
                    ublock_ct,
                    mblock_m,
                    mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c(),
                    op_output_row_major_scan_order);
                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
                
                if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                    int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                    int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                    epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                    log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                        op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                        input_num, input_name, max_num_tiles, kernel_buf_size);
                }
               
            }   
        } else if (n2p::get_op_class(op_info) == OpClass::Nary) {
            tt_dim_info input_dim = op_info.input_dims.at(input_num);
            if (op_info.attributes.splice_mode == SpliceMode::Ublock) {
                tt_grid_shape input_grid_shape = op_info.input_core_grids.at(input_num);
                bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);
                // input_src_tm should provide canonical [rt,ct,t] shape for input.  
                // Need to account for reblocking to the consuming op ublock shape + grid size, but input t should be just the output t
                int mblock_m = input_src_tm.get_size(map_dims::rt) / (op_info.grid_size_logical_r()*op_info.output_dim.ublock_rt);
                int mblock_n = input_src_tm.get_size(map_dims::ct) / (op_info.grid_size_logical_c()*op_info.output_dim.ublock_ct); 
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    op_info.output_dim.t,
                    op_info.output_dim.ublock_rt,
                    op_info.output_dim.ublock_ct,
                    mblock_m,
                    mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c(),
                    op_output_row_major_scan_order);
                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            } else if (op_info.attributes.splice_mode == SpliceMode::T){
                tt_grid_shape input_grid_shape = op_info.input_core_grids.at(input_num);
                bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);
                // input_src_tm should provide canonical [rt,ct,t] shape for input.  
                // Need to account for reblocking to the consuming op ublock shape + grid size.
                int t = input_src_tm.get_size(map_dims::t); 
                int mblock_m = input_src_tm.get_size(map_dims::rt) / (op_info.grid_size_logical_r()*op_info.output_dim.ublock_rt);
                int mblock_n = input_src_tm.get_size(map_dims::ct) / (op_info.grid_size_logical_c()*op_info.output_dim.ublock_ct);  
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    t,
                    op_info.output_dim.ublock_rt,
                    op_info.output_dim.ublock_ct,
                    mblock_m,
                    mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c(),
                    op_output_row_major_scan_order);
                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            }
        } else if (n2p::get_op_class(op_info) == OpClass::Buffer) {
            if (input_num == 0) {
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_col_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    op_info.output_dim.t,
                    op_info.attributes.u_kt,
                    op_info.output_dim.ublock_ct,
                    op_info.attributes.m_k,
                    op_info.output_dim.mblock_n,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c());

                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            } else if (input_num == 1) {
                consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_row_input(
                    kernel_bcast_tiles,
                    kernel_bcast_tiles_per_t,
                    1,
                    1,
                    1,
                    1,
                    op_info.attributes.num_index_tiles,
                    op_info.grid_size_logical_r(),
                    op_info.grid_size_logical_c());
                epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            }    
        } else if (n2p::get_op_class(op_info) == OpClass::Embedding) {

            tt_dim_info input_dim = op_info.input_dims.at(input_num);
            std::string table_input_name = op_info.input_names[input_num];
            tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
            if (input_num == 0) {
              consumer_to_producer_tile_map input_tm = input_src_tm.get_embedding_table_input(op_info.grid_size_logical_r(),
                                                                                              op_info.grid_size_logical_c(),
                                                                                              op_info.attributes.num_indices);
              epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            } else {
              consumer_to_producer_tile_map input_tm = input_src_tm.get_op_matmul_row_input(
                  kernel_bcast_tiles,
                  kernel_bcast_tiles_per_t,
                  1,
                  1,
                  1,
                  1,
                  input_dim.mblock_n,
                  op_info.grid_size_logical_r(),
                  op_info.grid_size_logical_c(),
                  true);
              epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
          }
        } else if (n2p::get_op_class(op_info) == OpClass::Tilizer) {
              std::string table_input_name = op_info.input_names[input_num];
              tt_queue_info input_queue_info = this->parsed_netlist.queue_map.at(table_input_name);
              int num_rows = input_queue_info.dim.mblock_m * input_queue_info.dim.ublock_rt * tt::tile_dim_to_array(input_queue_info.tile_dim)[0];
              consumer_to_producer_tile_map input_tm = input_src_tm.get_untilized_input(op_info.grid_size_logical_r(),
                                                                                        op_info.grid_size_logical_c(),
                                                                                        num_rows);
              epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
        } else if (n2p::get_op_class(op_info) == OpClass::Topk) {
            int mblock_m = op_info.input_dims[0].mblock_m;
            int mblock_n = op_info.input_dims[0].mblock_n;

            int ublock_rt = op_info.input_dims[0].ublock_rt;
            int ublock_ct = op_info.input_dims[0].ublock_ct;

            bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);

            consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                kernel_bcast_tiles,
                kernel_bcast_tiles_per_t,
                op_info.output_dim.t,
                ublock_rt,
                ublock_ct,
                mblock_m,
                mblock_n,
                op_info.grid_size_logical_r(),
                op_info.grid_size_logical_c(),
                op_output_row_major_scan_order);
            epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
        } else {
            int mblock_m = op_info.output_dim.mblock_m;
            int mblock_n = op_info.output_dim.mblock_n;

            int ublock_rt = op_info.output_dim.ublock_rt;
            int ublock_ct = op_info.output_dim.ublock_ct;

            bool op_output_row_major_scan_order = producer_output_row_major_ublock_scan_order(op_name, epoch_context);

            consumer_to_producer_tile_map input_tm = input_src_tm.get_op_eltwise_input(
                kernel_bcast_tiles,
                kernel_bcast_tiles_per_t,
                op_info.output_dim.t,
                ublock_rt,
                ublock_ct,
                mblock_m,
                mblock_n,
                op_info.grid_size_logical_r(),
                op_info.grid_size_logical_c(),
                op_output_row_major_scan_order);
            epoch_context.op_input_tm_pipes_map[op_name][input_name] = input_tm;
            
            if(is_name_prolog_queue(op_info.input_names.at(input_num), epoch_context)) {
                int max_num_tiles = n2p::get_max_tiles_at_output(input_tm);
                int kernel_buf_size = get_op_kernel_input_size_tiles(op_info, input_num, epoch_context);
                epoch_context.prolog_post_tm_operand[op_name][input_name] = max_num_tiles <= kernel_buf_size;
                log_debug(tt::LogNet2Pipe, "eltwise prolog for op '{}': enabled: '{}', input{}: '{}'; max num tiles prologed: {}, kernel_buf_size: {}",
                    op_name, max_num_tiles <= get_op_kernel_input_size_tiles(op_info, input_num, epoch_context),
                    input_num, input_name, max_num_tiles, kernel_buf_size);
            }
        }
    }
}

void Net2Pipe::collect_queue_input_pipes(const std::string &queue_name, temporal_epoch_context& epoch_context) const {

    tt_queue_info queue_info = this->parsed_netlist.queue_map.at(queue_name);
    if (!netlist_parser::is_queue_fed_by_op(queue_info)) {
        return;
    } else if (!epoch_context.op_info_map.count(queue_info.input)) {
        ERROR(std::string("Queue ") + queue_name + " - unknown input op: " + queue_info.input);
    }

    tt_op_info input_op_info = epoch_context.op_info_map.at(queue_info.input);

    if (input_op_info.untilize_output) {
        // FIXME imatosevic - presently we allow both formats for untilized queues; should we enforce stricter check (grid, ublock=1x1)?
        if (((queue_info.grid_size.r*queue_info.dim.mblock_m*queue_info.dim.ublock_rt) != (input_op_info.grid_size_logical_r() * input_op_info.output_dim.mblock_m * input_op_info.output_dim.ublock_rt)) ||
            ((queue_info.grid_size.c*queue_info.dim.mblock_n*queue_info.dim.ublock_ct) != (input_op_info.grid_size_logical_c() * input_op_info.output_dim.mblock_n * input_op_info.output_dim.ublock_ct)) ||
            (queue_info.dim.t != input_op_info.output_dim.t)) {
          ERROR(std::string("Queue ") + queue_name + " has incompatible block/grid dimensions with untilized input op: " + queue_info.input);
        }
        if (queue_info.input_tm_ops.size() > 0) {
          ERROR(std::string("Queue ") + queue_name + " has TMs on untilized input");
        }

        for (int r = 0; r < queue_info.grid_size.r; r++) {
            for (int c = 0; c < queue_info.grid_size.c; c++) {
                std::uint64_t buf_unique_id = this->queue_name_buf_unique_id_map.at(queue_name).at(r).at(c);

                std::vector<std::uint64_t> pipe_outputs = {buf_unique_id};
                std::vector<std::uint64_t> pipe_inputs = get_queue_pipe_input(queue_info, r, c, epoch_context);
                // queue input pipes never implement gather, reblocking, or TMs
                const auto &first_input_location = epoch_context.buffer_map.at(pipe_inputs.at(0)).core_location();
                auto pipe_location = first_input_location;
                std::uint64_t pipe_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
                // input/output NOCs are assigned after router pass (since for WH we need info on DRAM sub-channels)
                log_debug(tt::LogNet2Pipe, "CREATING UNTILIZING QUEUE INPUT PIPE {}:  input_buffer_ids: {}, output_buffer_ids: {}", pipe_unique_id, fmt::join(pipe_inputs, ", "), fmt::join(pipe_outputs, ", "));
                epoch_context.pipes.insert(
                    {pipe_unique_id, pipe_t({pipe_location}, pipe_inputs, pipe_outputs, queue_name, 0)});
                this->register_pipe_as_output_of_buffers(pipe_unique_id, pipe_inputs, epoch_context);
                this->register_pipe_as_input_of_buffers(pipe_unique_id, pipe_outputs, epoch_context);
            }
        }
        return;
    }    
    else {
        consumer_to_producer_tile_map tile_map = epoch_context.queue_input_tm_pipes_map[queue_name];
        for (auto pipe_it : tile_map.pipes) {
            phase_pipe_tile_map pipe = pipe_it.second;
            std::uint64_t pipe_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
            std::vector<std::vector<std::uint64_t>> pipe_outputs;
            for (auto it : pipe.dest_cores) {
                std::vector<std::uint64_t> curr_output;
                int r = it.first;
                int c = it.second;
                std::uint64_t buf_unique_id = this->queue_name_buf_unique_id_map.at(queue_name).at(r).at(c);
                curr_output.push_back(buf_unique_id);
                pipe_outputs.push_back(curr_output);
            }
            std::vector<std::tuple<chip_id_t, int, int>> pipe_coords;
            bool all_inputs_on_same_core = true;
            int last_input_core_r = -1;
            int last_input_core_c = -1;
            for (tile_to_core_index_map it : pipe.tile_map) {
                int input_core_r = it.core_r;
                int input_core_c = it.core_c;
                if (last_input_core_r == -1) {
                    last_input_core_r = input_core_r;
                    last_input_core_c = input_core_c;
                } else if (last_input_core_r != input_core_r || last_input_core_c != input_core_c) {
                    all_inputs_on_same_core = false;
                    break;
                }
            }
            TT_ASSERT(all_inputs_on_same_core); // This is supposed to be already checked when we generate the tile map
            chip_id_t producer_chip = netlist_utils::is_valid_ethernet_op(epoch_context.op_info_map.at(queue_info.input).type) ? 
                epoch_context.op_info_map.at(queue_info.input).attributes.ethernet_datacopy_attr.dest_device :
                this->op_graph_map.at(queue_info.input).target_device;
            int pipe_routing_x, pipe_routing_y;
            this->get_op_core_routing_cxy(epoch_context, queue_info.input, last_input_core_r, last_input_core_c, pipe_routing_y, pipe_routing_x);
            std::vector<tt_cxy_pair> pipe_coords_cxy = {tt_cxy_pair(producer_chip, pipe_routing_x, pipe_routing_y)};

            std::vector<std::uint64_t> pipe_inputs;
            int granularity_index = 0;
            const int scatter_granularity = epoch_context.op_queue_output_buf_granularity.at(queue_info.input);
            for (tile_to_core_index_map it : pipe.tile_map) {
                int input_core_r = it.core_r;
                int input_core_c = it.core_c;
                int input_tile_index = it.tile_index;
                //log_debug(tt::LogNet2Pipe, "{},{}-{}", input_core_r, input_core_c, input_tile_index);
                if ((granularity_index % scatter_granularity) == 0) {
                    std::uint64_t producer_output_buf_start_unique_id = epoch_context.op_output_buf_map.at(queue_info.input).at(input_core_r).at(input_core_c);
                    std::uint64_t producer_output_buf_unique_id = producer_output_buf_start_unique_id + input_tile_index;
                    pipe_inputs.push_back(producer_output_buf_unique_id);
                }
                granularity_index++;
            }

            for(auto buf_id: pipe_inputs) {
                if(epoch_context.buffer_map.find(buf_id) == epoch_context.buffer_map.end() ) {
                    log_warning(tt::LogNet2Pipe, "Queue: {} ", queue_name);
                    tile_map.print();
                    log_fatal("Pipe input buf id {} could not be found in buffer_map. Scatter granularity: {}", buf_id ,scatter_granularity);
                }
            }

            const auto &output_buf_ids = pipe_outputs.at(0);
            const auto &location = pipe_coords_cxy.at(0);
            log_debug(tt::LogNet2Pipe, "CREATING QUEUE INPUT PIPE {} @ location (c={},y={},x={}). DRAM_READ? {}. DRAM_WRITE? {}. input_buffer_ids: {}, output_buffer_ids: {}", 
                pipe_unique_id,
                location.chip, location.y, location.x, 
                std::any_of(pipe_inputs.begin(),pipe_inputs.end(), [this, &epoch_context](auto id) { return epoch_context.buffer_map.at(id).is_queue();}) ? "Y" : "N",
                std::any_of(output_buf_ids.begin(),output_buf_ids.end(), [this, &epoch_context](auto id) { return epoch_context.buffer_map.at(id).is_queue();}) ? "Y" : "N",
                fmt::join(pipe_inputs, ", "), fmt::join(output_buf_ids, ", ")
                );
            auto router_pipe = pipe_t(location, pipe_inputs, output_buf_ids, queue_name, 0);

            if(input_op_info.gradient_op) {
                // Keep track of pipes feeding gradient output Queues/RAMs. This data structure (along with this->pipes) is used to populate the queue to producers 
                // map used by runtime.
                epoch_context.grad_op_pipes.insert({pipe_unique_id, router_pipe});
            }
            else{
                epoch_context.pipes.insert({pipe_unique_id, router_pipe});
                this->register_pipe_as_output_of_buffers(pipe_unique_id, pipe_inputs, epoch_context);
                this->register_pipe_as_input_of_buffers(pipe_unique_id, output_buf_ids, epoch_context); 
            }
        }
        return;
    }
}


std::vector<std::uint64_t> Net2Pipe::get_queue_pipe_input(tt_queue_info queue_info, int r, int c, const temporal_epoch_context& epoch_context) const {
    std::vector<std::uint64_t> result;
    int scatter_gather_num_tiles;
    if (epoch_context.op_info_map.at(queue_info.input).untilize_output) {
        int grid_size_r = epoch_context.op_info_map.at(queue_info.input).grid_size_logical_r();
        int grid_size_c = epoch_context.op_info_map.at(queue_info.input).grid_size_logical_c();
        for (int i = 0; i < grid_size_r; i++) {
            for (int j = 0; j < grid_size_c; j++) {
                result.push_back(epoch_context.op_output_buf_map.at(queue_info.input).at(i).at(j));
            }
        }
    } else if (is_output_scatter(epoch_context.op_info_map.at(queue_info.input).name, scatter_gather_num_tiles, epoch_context)) {
        int mblock_tiles = n2p::get_op_output_mblock_tiles(epoch_context.op_info_map.at(queue_info.input));
        int replicate = (n2p::get_op_output_single_buf_size_mblocks(epoch_context.op_info_map.at(queue_info.input)) * mblock_tiles) / scatter_gather_num_tiles;
        for (int k = 0; k < replicate; k++) {
            result.push_back(epoch_context.op_output_buf_map.at(queue_info.input).at(r).at(c) + k*scatter_gather_num_tiles);
        }
    } else {
        result.push_back(epoch_context.op_output_buf_map.at(queue_info.input).at(r).at(c));
    }

    // Validation
    for (const std::uint64_t buf_id : result) {
        TT_ASSERT(epoch_context.buffer_map.find(buf_id) != epoch_context.buffer_map.end(), "Buffer ID not found in buffer map: ", buf_id);
    }
    return result;
}

void Net2Pipe::collect_op_input_pipes(const std::string &op_name, int input_count, temporal_epoch_context& epoch_context) const {

    // auto get_tile_producer_routing_coord = [this](const std::string &input_name, int op_input_producer_chip, const tile_to_core_index_map &tile_core_index_map) -> tt_cxy_pair {
    //     bool is_queue = this->name_is_queue(input_name);
    //     int r = tile_core_index_map.core_r;
    //     int c = tile_core_index_map.core_c;
    //     if (is_queue) {
    //         const auto &queue_info = this->parsed_netlist.queue_map.at(input_name);
    //         TT_ASSERT(queue_info.loc == QUEUE_LOCATION::DRAM);
    //         auto dram_channel = get_queue_dram_channel(queue_info, r, c);
    //         // TODO(snijjar): get subchannel info added to netlist definition
    //         auto dram_subchannel = 0;
    //         const auto &dram_core_location = this->soc_descriptor->dram_cores.at(dram_channel).at(dram_subchannel);
    //         return tt_cxy_pair(queue_info.target_device, dram_core_location.x, dram_core_location.y);
    //     } else {
    //         tt_op_info producer_op_info = epoch_context.op_info_map[input_name];
    //         int producer_core_log_y = producer_op_info.grid_loc[0] + r;
    //         int producer_core_log_x = producer_op_info.grid_loc[1] + c;
    //         return tt_cxy_pair(
    //             op_input_producer_chip, 
    //             this->soc_descriptor->worker_log_to_routing_x.at(producer_core_log_x), 
    //             this->soc_descriptor->worker_log_to_routing_y.at(producer_core_log_y)
    //         );
    //     }
    // };

    auto get_op_input_producer_chip = [&](const std::string &name) {
        if (epoch_context.op_info_map.find(name) != epoch_context.op_info_map.end()) {
            const auto &op_info = epoch_context.op_info_map.at(name);
            if (netlist_utils::is_valid_ethernet_op(op_info.type)) {
                return op_info.attributes.ethernet_datacopy_attr.dest_device;
            } else {
                return this->op_graph_map.at(name).target_device;
            }
        } else if (name_is_queue(name)) {
            return this->parsed_netlist.queue_map.at(name).target_device;
        } else {
            log_fatal("Node is not an a valid ethernet op or queue");
            return -1;
        }
    };
    
    tt_op_info op_info = epoch_context.op_info_map.at(op_name);

    int num_input_bufs = n2p::get_op_num_input_bufs(op_info);

    for (int input_index = 0; input_index < num_input_bufs; input_index++) {
        std::string input_name = epoch_context.op_input_name_map[op_name][input_index];
        consumer_to_producer_tile_map tile_map = epoch_context.op_input_tm_pipes_map[op_name][input_name];
        const int tile_clear_granularity = this->get_op_kernel_input_tile_clear_granularity(op_info, input_index);
        bool embedding_op = (n2p::get_op_class(op_info) == OpClass::Embedding);
        bool tilizer_op = (n2p::get_op_class(op_info) == OpClass::Tilizer);
        bool tiles_no_header = (embedding_op || tilizer_op) && (input_index == 0);
        int tile_size_bytes = n2p::get_format_tile_size_bytes(op_info.input_data_formats[input_index],
                                                         !tiles_no_header, op_info.input_tile_dims[input_index]);

        int prolog_consumer_index = 0;
        if (name_is_queue(input_name)) {
          if (is_name_prolog_queue(input_name, epoch_context) and (epoch_context.op_queue_output_map[input_name].size() > 1)) {
            // prolog queues that fork out to multiple ops have their corresponding buffers in pipegen.yaml
            // replicated for each consumer
            std::string graph_name = this->op_graph_map.at(op_name).name;
            bool prolog_consumer_index_found = false;
            std::set<std::string> unique_ops;
            //log_debug(tt::LogNet2Pipe, "graph_name: {}", graph_name);
            //log_debug(tt::LogNet2Pipe, "op_name: {}", op_name);
            for (std::string connected_op_name : epoch_context.op_queue_output_map[input_name]) {
                //log_debug(tt::LogNet2Pipe, "connected_op_name: {}", connected_op_name);
              if (connected_op_name == op_name) {
                prolog_consumer_index_found = true;
                break;
              } 
              else if (this->op_graph_map.at(connected_op_name).name == graph_name and unique_ops.find(connected_op_name) == unique_ops.end() ) {
                unique_ops.insert(connected_op_name);
                prolog_consumer_index++;
              }
            }
            if (!prolog_consumer_index_found) {
              ERROR(std::string("Prolog queue ") + input_name + " -> can't find consumer index for op " + op_name);
            }
          }
          /*
          // FIXME imatosevic - revise this warning
          if (!queue_setting_map[input_name].prolog) {
            const uint32_t MIN_DRAM_EFFICIENT_READ_SIZE_BYTES = 4096; // FIXME imatosevic - adjust this threshold for GS vs. WH
            int dram_read_granularity = tile_map.scatter_granularity * tile_size_bytes;
            if (dram_read_granularity < MIN_DRAM_EFFICIENT_READ_SIZE_BYTES) {
              n2p::Log() << "\nWARNING - op " << op_name << ", has streaming DRAM input "
                        << input_name << " with read granularity of " 
                        << tile_map.scatter_granularity << " tiles (" 
                        << dram_read_granularity << " bytes), should be at least ~4KB for efficient reads\n\n";
            }
          }
          */
        }
        
        auto process_pipes = [this, input_name, input_index, op_name, op_info, prolog_consumer_index, tile_clear_granularity, &epoch_context](
            consumer_to_producer_tile_map& tile_map,
            bool has_two_step_prolog,
            bool step1,
            bool step2
        ) {
        bool is_ethernet_datacopy = netlist_utils::is_valid_ethernet_op(op_info.type);
        auto pipe_coord_lookup_function = [this](const std::tuple<chip_id_t,int,int> &pipe_coord, const tt_op_info &op_info, bool is_ethernet_datacopy) -> tt_cxy_pair {
            if (is_ethernet_datacopy) {
                // The location should allready be mapped to the routing coord
                return tt_cxy_pair(std::get<0>(pipe_coord), std::get<2>(pipe_coord), std::get<1>(pipe_coord));
            } else {
                chip_id_t chip = std::get<0>(pipe_coord);
                return tt_cxy_pair(
                    chip, 
                    this->soc_descriptors.at(chip).worker_log_to_routing_x.at(std::get<2>(pipe_coord)), 
                    this->soc_descriptors.at(chip).worker_log_to_routing_y.at(std::get<1>(pipe_coord))
                );
            }
        };

        const int scatter_granularity = step2 ? tile_map.scatter_granularity : epoch_context.op_queue_output_buf_granularity.at(input_name);

        for (auto pipe_it : tile_map.pipes) {
            
            phase_pipe_tile_map pipe = pipe_it.second;
            if(not pipe.validate_padding()) {
                log_warning(tt::LogNet2Pipe, "Error at Op: {}, Input {}: {} ", op_name, input_index, input_name);
                tile_map.print();
                log_fatal("Pipe contains only padding as input");
            }
            std::uint64_t pipe_unique_id = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
            
            std::vector<std::vector<std::uint64_t>> pipe_outputs;

            auto& in_buf_map = (has_two_step_prolog and step1) ? epoch_context.prolog_buffer_map : epoch_context.op_input_buf_map;
            for (auto it : pipe.dest_cores) {
                std::vector<std::uint64_t> curr_output;
                int core_offset_r = it.first;
                int core_offset_c = it.second;
                if (core_offset_r == -1) {  // column multicast
                    assert( not (has_two_step_prolog and step1));
                    for (int r = 0; r < op_info.grid_size_logical_r(); r++) {
                        curr_output.push_back(in_buf_map[op_name][r][core_offset_c][input_index]);
                    }
                } else if (core_offset_c == -1) {  // row multicast
                    assert( not (has_two_step_prolog and step1));
                    for (int c = 0; c < op_info.grid_size_logical_c(); c++) {
                        curr_output.push_back(in_buf_map[op_name][core_offset_r][c][input_index]);
                    }
                } else {
                    curr_output.push_back(in_buf_map[op_name][core_offset_r][core_offset_c][input_index]);
                }
                pipe_outputs.push_back(curr_output);
            }

            std::vector<std::tuple<chip_id_t, int, int>> pipe_coords;
            bool pipe_coords_are_ethernet_channels = false; //name_is_op(input_name) && netlist_utils::is_valid_ethernet_op(epoch_context.op_info_map.at(input_name).type);
            get_op_input_pipe_mcast_core_rc(op_info, epoch_context, input_index, pipe, pipe_outputs, pipe_coords, pipe_coords_are_ethernet_channels);
            TT_ASSERT(pipe_outputs.size() == pipe_coords.size());

            std::vector<std::uint64_t> pipe_inputs;
            int granularity_index = 0;
            //log_debug(tt::LogNet2Pipe, "scatter_granularity: {}", scatter_granularity);
            //log_debug(tt::LogNet2Pipe, "tile_map:");
            // TODO: Can we not just iterate through pipe.tile_map in increments of scatter granularity?
            for (tile_to_core_index_map it : pipe.tile_map) {
                int input_core_r = it.core_r;
                int input_core_c = it.core_c;
                int input_tile_index = it.tile_index;
                //log_debug(tt::LogNet2Pipe, "{},{}-{}", input_core_r, input_core_c, input_tile_index);
                if ((granularity_index % scatter_granularity) == 0) {
                  if(it.is_padding_tile) {

                    const auto [chip_buffer_loc, core_y_buffer_loc, core_x_buffer_loc] = pipe_coords.at(0);

                    uint64_t padding_buffer_id = get_pad_buffer_id(
                        op_info,
                        epoch_context,
                        input_index,
                        chip_buffer_loc,
                        core_y_buffer_loc,
                        core_x_buffer_loc
                    );
                    pipe_inputs.push_back(padding_buffer_id);
                  }
                  else {
                    std::uint64_t output_buf_start_unique_id;
                    if (name_is_queue(input_name)) {
                        if (is_name_prolog_queue(input_name, epoch_context)) {
                            if (has_two_step_prolog && step2) {
                                output_buf_start_unique_id =  epoch_context.prolog_buffer_map.at(op_name).at(input_core_r).at(input_core_c).at(input_index);
                            }
                            else {
                                output_buf_start_unique_id = epoch_context.prolog_queue_name_fork_buf_unique_id_map.at(input_name).at(input_core_r).at(input_core_c).at(prolog_consumer_index);
                            }
                        }
                        else {
                            output_buf_start_unique_id = this->queue_name_buf_unique_id_map.at(input_name).at(input_core_r).at(input_core_c);
                        }
                    } else {
                        output_buf_start_unique_id = epoch_context.op_output_buf_map.at(input_name).at(input_core_r).at(input_core_c);
                    }
                    std::uint64_t output_buf_unique_id = output_buf_start_unique_id + input_tile_index;
                    pipe_inputs.push_back(output_buf_unique_id);
                  }
                }
                granularity_index++;
            }

            std::vector<tt_cxy_pair> pipe_coords_cxy = {};
            
            for (size_t i = 0; i < pipe_outputs.size(); i++) {
                const auto &pipe_coord = pipe_coords.at(i);
                pipe_coords_cxy.push_back(pipe_coord_lookup_function(pipe_coord, op_info, pipe_coords_are_ethernet_channels));
            }

            std::vector<std::uint64_t> output_padding_list = {};
            if(pipe_outputs.size() > 1) {
                for (size_t i = 0; i < pipe_outputs.size(); i++) {
                    if(pipe.padding_output_list.at(i)) { // TODO: fix this initialization
                        const auto &pipe_coord = pipe_coords.at(i);
                        const chip_id_t chip_buffer_loc = std::get<0>(pipe_coord);
                        const int core_y_buffer_loc = std::get<1>(pipe_coord);
                        const int core_x_buffer_loc = std::get<2>(pipe_coord);
                        uint64_t padding_buffer_id = get_pad_buffer_id(
                            op_info,
                            epoch_context,
                            input_index,
                            chip_buffer_loc,
                            core_y_buffer_loc,
                            core_x_buffer_loc
                        );
                        output_padding_list.push_back(padding_buffer_id);
                    }
                    else {
                        output_padding_list.push_back(0);
                    }
                    
                }
            }

            TT_ASSERT(pipe_coords_cxy.size() == pipe_outputs.size());
            for(auto buf_id: pipe_inputs) {
                if(epoch_context.buffer_map.find(buf_id) == epoch_context.buffer_map.end() ) {
                    log_warning(tt::LogNet2Pipe, "Op: {}, Input {}: {} ", op_name, input_index, input_name);
                    tile_map.print();
                    log_fatal("Pipe input buf id {} could not be found in buffer_map. Scatter granularity: {}", buf_id ,scatter_granularity);
                }
            }

            if (pipe_outputs.size() == 1) { // This is for default non-scatter pipes
                const auto &output_buf_ids = pipe_outputs.at(0);
                const auto &location = pipe_coords_cxy.at(0);
                log_debug(tt::LogNet2Pipe, "CREATING PIPE {} @ location (c={},y={},x={}). DRAM_READ? {}. DRAM_WRITE? {}. input_buffer_ids: {}, output_buffer_ids: {}", 
                    pipe_unique_id,
                    location.chip, location.y, location.x, 
                    std::any_of(pipe_inputs.begin(),pipe_inputs.end(), [this, &epoch_context](auto id) { return epoch_context.buffer_map.at(id).is_queue();}) ? "Y" : "N",
                    std::any_of(output_buf_ids.begin(),output_buf_ids.end(), [this, &epoch_context](auto id) { return epoch_context.buffer_map.at(id).is_queue();}) ? "Y" : "N",
                    fmt::join(pipe_inputs, ", "), fmt::join(output_buf_ids, ", ")
                    );
                auto router_pipe = pipe_t(location, pipe_inputs, output_buf_ids, op_name, input_index, tile_clear_granularity);
                epoch_context.pipes.insert({pipe_unique_id, router_pipe});
                this->register_pipe_as_output_of_buffers(pipe_unique_id, pipe_inputs, epoch_context);
                this->register_pipe_as_input_of_buffers(pipe_unique_id, output_buf_ids, epoch_context);
            } else { // This is for Scatter Pipes
                // TODO(snijjar): correctly compute the scatter attribute for each timestep
                std::stringstream ss;
                log_debug(tt::LogNet2Pipe, "CREATING SCATTER PIPE {}: input_buffer_ids: {}, output_buffer_ids:", pipe_unique_id, fmt::join(pipe_inputs, ", "));
                for (auto const &output_buf_ids : pipe_outputs) log_debug(tt::LogNet2Pipe, "  - {}", fmt::join(output_buf_ids, ", "));
                auto router_pipe = pipe_t(pipe_coords_cxy, pipe_inputs, pipe_outputs, op_name, input_index, tile_clear_granularity);
                router_pipe.output_padding_buffer_list = output_padding_list;
                epoch_context.pipes.insert({pipe_unique_id, router_pipe});
                this->register_pipe_as_output_of_buffers(pipe_unique_id, pipe_inputs, epoch_context);
                this->register_pipe_as_input_of_buffers(pipe_unique_id, pipe_outputs, epoch_context);
            }

            if (pipe_coords.size() > 1) {
                log_debug(tt::LogRouter, "MULTIPLE PIPE COORDS");
            }
        }
        };
        const bool has_two_step_prolog = epoch_context.prolog_tm_pipes_map.find(op_name) != epoch_context.prolog_tm_pipes_map.end() and 
                                         epoch_context.prolog_tm_pipes_map.at(op_name).find(input_name) != epoch_context.prolog_tm_pipes_map.at(op_name).end();
        if (has_two_step_prolog) {
            log_debug(tt::LogNet2Pipe, "pipe @ '{}' : two step 0", input_name);
            auto& tm0 = epoch_context.prolog_tm_pipes_map.at(op_name).at(input_name);
            //tm0.print();
            process_pipes(epoch_context.prolog_tm_pipes_map.at(op_name).at(input_name), true, true, false);
            log_debug(tt::LogNet2Pipe, "pipe @ '{}' : two step 1", input_name);
            auto& tm1 = epoch_context.prolog_input_pipes_map.at(op_name).at(input_name);
            //tm1.print();
            process_pipes(epoch_context.prolog_input_pipes_map.at(op_name).at(input_name), true, false, true);
        }
        else {
            consumer_to_producer_tile_map tile_map = epoch_context.op_input_tm_pipes_map[op_name][input_name];
            // For producer queues we may need to force smaller granularity, since it can't be larger
            // than the destination op input buffer due to nrisc fw requirements.
            // For producer ops we need to force the smallest granularity in case of fork.
            log_debug(tt::LogNet2Pipe, "pipe @ '{}' : regular (non-two step)", input_name);
            process_pipes(tile_map, false, false, false);
        }
    }
}

bool Net2Pipe::is_ethernet_pipe(const pipe_t &pipe, const temporal_epoch_context& epoch_context) const {
    if (not this->config.is_feature_ethernet_multichip_compile_enabled) {
        return false;
    }
    bool is_unicast =
        pipe.input_buffer_ids.size() == 1 && (!pipe.has_multiple_timesteps() && pipe.output_buffer_ids().size() == 1);
    if (is_unicast) {
        std::size_t producer_chip = epoch_context.buffer_map.at(pipe.input_buffer_ids.at(0)).core_location().chip;
        std::size_t consumer_chip = epoch_context.buffer_map.at(pipe.output_buffer_ids().at(0)).core_location().chip;
        bool consumer_is_sys_dram =
          (epoch_context.buffer_map.at(pipe.output_buffer_ids().at(0)).core_location().x == 255) &&
          (epoch_context.buffer_map.at(pipe.output_buffer_ids().at(0)).core_location().y == 255);
        return !consumer_is_sys_dram && (producer_chip != consumer_chip);
    } else {
        return false;
    }
}

void Net2Pipe::get_queue_consumer_map(
    std::map<router::unique_id_t, std::vector<router::unique_id_t>> inputs_to_output_pipes_map, temporal_epoch_context& epoch_context) const {

    for (auto input_to_output_pipe_list: inputs_to_output_pipes_map) {

        router::unique_id_t input_id = input_to_output_pipe_list.first;
        vector<router::unique_id_t> const& output_pipe_ids = input_to_output_pipe_list.second;

        TT_ASSERT(epoch_context.buffer_map.find(input_id) != epoch_context.buffer_map.end(), "All input and output ids extracted from pipe inputs and outputs must exist in buffer_map");
        bool is_input_queue = epoch_context.buffer_map.at(input_id).is_queue();
        if (is_input_queue) {
            if (epoch_context.input_queue_id_to_consumer_cores.find(input_id) == epoch_context.input_queue_id_to_consumer_cores.end()) {
                epoch_context.input_queue_id_to_consumer_cores.insert({input_id, {}});
            }

            for (auto output_pipe_id: output_pipe_ids) {
                TT_ASSERT(epoch_context.pipes.find(output_pipe_id) != epoch_context.pipes.end(), "All input and output ids extracted from pipe inputs and outputs must exist in buffer_map");
                router::pipe_t const& output_pipe = epoch_context.pipes.at(output_pipe_id);
                bool is_output_op = !(epoch_context.buffer_map.at(output_pipe.output_segment_output_buffer_ids(0).at(0)).is_queue());
                if (is_output_op) {
                    for (tt_cxy_pair const& output_op_coord : output_pipe.core_locations()) {
                        epoch_context.input_queue_id_to_consumer_cores.at(input_id).insert(output_op_coord);
                    }
                }
            }
        } else if (epoch_context.prologue_buf_unique_id_to_queue_info.find(input_id) != epoch_context.prologue_buf_unique_id_to_queue_info.end()) {
            string queue_name = get<0>(epoch_context.prologue_buf_unique_id_to_queue_info.at(input_id));
            tt_xy_pair loc = get<1>(epoch_context.prologue_buf_unique_id_to_queue_info.at(input_id));
            int conn_op_index = get<2>(epoch_context.prologue_buf_unique_id_to_queue_info.at(input_id));
            uint64_t unique_q_id = epoch_context.queue_name_unique_id_map.at(queue_name);
            if (epoch_context.input_queue_id_to_consumer_cores.find(unique_q_id) == epoch_context.input_queue_id_to_consumer_cores.end()) {
                epoch_context.input_queue_id_to_consumer_cores.insert({unique_q_id, {}});
            }
            std::string op_name = epoch_context.op_queue_output_map[queue_name][conn_op_index];
            int y_coord, x_coord;
            epoch_context.op_info_map.at(op_name).get_core_yx_coord(loc.y, loc.x, y_coord, x_coord);

            chip_id_t chip = this->op_graph_map.at(op_name).target_device;
            const auto &routing_core_coordinates = tt_cxy_pair(
                chip,
                this->soc_descriptors.at(chip).worker_log_to_routing_x.at(x_coord),
                this->soc_descriptors.at(chip).worker_log_to_routing_y.at(y_coord));
            
            epoch_context.input_queue_id_to_consumer_cores.at(unique_q_id).insert(routing_core_coordinates);
        }
    }
}

void Net2Pipe::get_queue_producer_map(
    std::map<router::unique_id_t, std::vector<router::unique_id_t>> inputs_to_output_pipes_map, temporal_epoch_context& epoch_context) const {
    for(auto input_to_output_pipe_list : inputs_to_output_pipes_map) {
        router::unique_id_t input_id = input_to_output_pipe_list.first;
        vector<router::unique_id_t> output_pipe_ids = input_to_output_pipe_list.second;

        TT_ASSERT(epoch_context.buffer_map.find(input_id) != epoch_context.buffer_map.end(), "All input and output ids extracted from pipe inputs and outputs must exist in buffer_map");
        bool is_input_op = !(epoch_context.buffer_map.at(input_id).is_queue());

        if(is_input_op) {
            tt_cxy_pair input_op_coord = epoch_context.buffer_map.at(input_id).core_location();
            for(router::unique_id_t output_pipe_id : output_pipe_ids) {
                router::pipe_t const& pipe = epoch_context.pipes.at(output_pipe_id);
                int num_pipe_segments = pipe.number_of_output_segments();
                for (int s = 0; s < num_pipe_segments; s++) {
                    for (router::unique_id_t output_id : pipe.output_segment_output_buffer_ids(s)) {
                        if(epoch_context.buffer_map.at(output_id).is_queue()) {
                            if(epoch_context.output_queue_id_to_producer_cores.find(output_id) == epoch_context.output_queue_id_to_producer_cores.end()) {
                                epoch_context.output_queue_id_to_producer_cores.insert({output_id, {}});
                            }
                            epoch_context.output_queue_id_to_producer_cores.at(output_id).insert(input_op_coord);
                        }
                    }
                }
            }
        }
    }
}

void Net2Pipe::dump_queue_to_core_map_to_file(const std::string& output_dir, bool queue_to_producer, const temporal_epoch_context& epoch_context) const {
    YAML::Node output_yaml;
    std::string out_file_name = output_dir + (queue_to_producer ? "queue_to_producer.yaml" : "queue_to_consumer.yaml");
    std::ofstream output_file(out_file_name);
    std::string cores_tag = queue_to_producer ? "producers" : "consumers";
    uint32_t core_idx = 0;
    unordered_map<queue_buffer_info, set<tt_cxy_pair>> queue_buf_to_cores;

    const auto& queue_id_to_core = queue_to_producer ? epoch_context.output_queue_id_to_producer_cores : epoch_context.input_queue_id_to_consumer_cores;
    for(const auto& queue : queue_id_to_core) {
        router::unique_id_t q_id = queue.first;
        const router::router_buffer_info_t& buffer_info = epoch_context.buffer_map.at(q_id);\
        const router::router_queue_allocation_info queue_location = buffer_info.queue_info().get_allocation_info();
        const router::unique_id_t queue_unique_id = buffer_info.queue_info().get_parent_queue_id();
        if (queue_unique_id == router::NO_ASSOCIATED_QUEUE_OBJECT_ID) {
            continue;
        }

        TT_ASSERT(epoch_context.queue_unique_id_info_map.find(queue_unique_id) != epoch_context.queue_unique_id_info_map.end());
        tt_queue_info queue_info = epoch_context.queue_unique_id_info_map.at(queue_unique_id);
        string queue_name = queue_info.name;

        queue_buffer_info queue_buf_info = {.queue_name=queue_name, .alloc_info=queue_location};
        if (queue_buf_to_cores.find(queue_buf_info) == queue_buf_to_cores.end()) {
            queue_buf_to_cores.insert({queue_buf_info, {}});
        }
        queue_buf_to_cores.at(queue_buf_info).insert(queue.second.begin(), queue.second.end());
    }

    for(const auto& buf_it : queue_buf_to_cores) {
        string queue_name = buf_it.first.queue_name;
        auto queue_location = router::router_queue_allocation_info{buf_it.first.alloc_info};
        uint64_t unique_q_id = epoch_context.queue_name_unique_id_map.at(queue_name);
        tt_queue_info queue_info = epoch_context.queue_unique_id_info_map.at(unique_q_id);
        YAML::Node queue_yaml;
        queue_yaml["name"] = queue_name;
        queue_yaml["channel"] = queue_location.channel;
        queue_yaml["queue_target_device"] = queue_info.target_device;
        queue_yaml["addr"] = uint_to_string(queue_location.address);

        uint buf_idx = 0;
        for(; buf_idx < queue_info.alloc_info.size(); buf_idx++) {
            if(queue_info.alloc_info.at(buf_idx) == queue_location) {
                break;
            }
        }
        TT_ASSERT(buf_idx != queue_info.alloc_info.size(), "The queue-buffer location extracted from buffer map should exist in queue_unique_id_info_map.");

        for(tt_cxy_pair core: buf_it.second) {
            YAML::Node core_yaml;
            core_yaml["chip_id"] = core.chip;
            core_yaml["x"] = core.x;
            core_yaml["y"] = core.y;
            queue_yaml[cores_tag][to_string(core_idx)] = core_yaml;
            core_idx++; 
        }
        output_yaml[queue_name][to_string(buf_idx)] = queue_yaml;
    }
    output_file << output_yaml;
    output_file.close();
}

void insert_new_input_to_output_entry(
        std::map<router::unique_id_t, std::vector<router::unique_id_t>> &inputs_to_output_pipes_map,
        std::vector<router::unique_id_t> const& input_list,
        std::vector<router::unique_id_t> const& output_pipe_list) {
    
    for (auto input: input_list) {
        if (inputs_to_output_pipes_map.find(input) == inputs_to_output_pipes_map.end()) {
            inputs_to_output_pipes_map.insert({input, {}});
        }
        for (auto output: output_pipe_list) {
            if (std::find(inputs_to_output_pipes_map.at(input).begin(), inputs_to_output_pipes_map.at(input).end(), output) == inputs_to_output_pipes_map.at(input).end()) {
                inputs_to_output_pipes_map.at(input).push_back(output);
            }
        }
    }
}

template<typename T>
vector<T> flatten_2d_vec(vector<vector<T>> input) {
    vector<T> output;
    for (auto row: input) {
        for (auto col: row) {
            output.push_back(col);
        }
    }
    return output;
}

bool Net2Pipe::is_mmio_pipe(const pipe_t &pipe, bool& is_downstream, const temporal_epoch_context& epoch_context) const {
  if (this->config.is_feature_ethernet_multichip_compile_enabled) {
    // MMIO pipes are not used on WH where we have ethernet
    return false;
  }
  else {
    std::size_t producer_device = epoch_context.buffer_map.at(pipe.input_buffer_ids.at(0)).core_location().chip;
    std::uint64_t first_output_buf_id = pipe.has_multiple_timesteps() ? pipe.time_multiplexed_output_buffer_ids().at(0).at(0) : pipe.output_buffer_ids().at(0);
    std::size_t consumer_device = epoch_context.buffer_map.at(first_output_buf_id).core_location().chip;
    bool producer_is_queue = epoch_context.buffer_map.at(pipe.input_buffer_ids.at(0)).is_queue();
    bool consumer_is_queue = epoch_context.buffer_map.at(first_output_buf_id).is_queue();
    if (producer_device != consumer_device && !producer_is_queue && !consumer_is_queue) {
      is_downstream = is_target_device_downstream(this->starting_device_id, this->ending_device_id, producer_device, consumer_device);
      return true;
    } else {
      return false;
    }
  }
}


int Net2Pipe::check_pipe_consumer_repeat(const pipe_t &pipe) const {
  if (pipe.has_consumer() && pipe.has_multiple_timesteps()) {
    return pipe_scatter_outputs_consumer_repeat(pipe.time_multiplexed_output_buffer_ids(), pipe.output_padding_buffer_list);
  } else {
    return 1;
  }
}
int Net2Pipe::pipe_scatter_outputs_consumer_repeat(const std::vector<std::vector<std::uint64_t>> &pipe_outputs, const std::vector<std::uint64_t>& output_id_padding) const {
    std::vector<bool> pipe_output_padding;
    for(auto & pad_id: output_id_padding) {
        pipe_output_padding.push_back(pad_id > 0);
    }
    return pipe_scatter_outputs_consumer_repeat(pipe_outputs, pipe_output_padding);
}

int Net2Pipe::pipe_scatter_outputs_consumer_repeat(const std::vector<std::vector<std::uint64_t>> &pipe_outputs, const std::vector<bool>& pipe_output_padding) const {
    // FIXME imatosevic - this info should be carried from tile_maps rather than recomputed here
    int consumer_list_size = pipe_outputs.size();
    if (pipe_outputs.size() == 1) {
        return 1;
    }
    const std::vector<router::unique_id_t>& first_consumer = pipe_outputs.at(0);
    int consumer_repeat = 0;
    for (const std::vector<router::unique_id_t>& consumer : pipe_outputs) {
        if (consumer == first_consumer) {
            consumer_repeat++;
        } else {
            break;
        }
    }
    if ((consumer_repeat < 2) || ((consumer_list_size % consumer_repeat) != 0)) {
        return 1;
    }
    for (int i = 0; i < consumer_list_size; i += consumer_repeat) {
        for (int j = i+1; j < (i+consumer_repeat); j++) {
            if (pipe_outputs.at(i) != pipe_outputs.at(j) or pipe_output_padding.at(i) != pipe_output_padding.at(j)) {
                return 1;
            }
        }
    }
    // also check that no destination appears in more than one repeating sequence (see #1237)
    // e.g. [ [A] [A] [B] [B] [C] [C] [D] [D] ... ] is OK, but [ [A] [A] [B] [B] [A] [A] [B] [B] ... ] isn't
    bool repeated_dest = false;
    for (int i = 0; i < consumer_list_size; i += consumer_repeat) {
        for (int j = 0; j < i; j += consumer_repeat) {
            if (pipe_outputs.at(j) == pipe_outputs.at(i)) {
                return 1;
            }
        }
    }
    return consumer_repeat;
}


bool Net2Pipe::check_pipe_inputs_periodic(const pipe_t& pipe, int& period, int& periodic_repeat, const temporal_epoch_context& epoch_context) const {

  static constexpr int MIN_PERIODIC_PIPE_UNROLL = 32;
  static constexpr int MAX_PERIODIC_PIPE_UNROLL = 64; // FIXME imatosevic - revise this

  int num_inputs = pipe.input_buffer_ids.size();
  if (num_inputs <= MAX_PERIODIC_PIPE_UNROLL) {
    return false;
  }

  std::vector<std::uint64_t> const& output_buffers = pipe.output_segment_output_buffer_ids(0);
  bool feeds_queue_buffers = std::any_of(output_buffers.begin(), output_buffers.end(), [this, &epoch_context](auto id) { return epoch_context.buffer_map.at(id).is_queue();});
  if (feeds_queue_buffers) {
    return false;
  }

  if (epoch_context.buffer_op_or_queue_name_map.find(pipe.input_buffer_ids.at(0)) == epoch_context.buffer_op_or_queue_name_map.end()) {
    return false;
  }
  std::string producer_name = epoch_context.buffer_op_or_queue_name_map.at(pipe.input_buffer_ids.at(0));
  
  int scatter_gather_num_tiles;
  if (!is_output_scatter(producer_name, scatter_gather_num_tiles, epoch_context)) {
    return false;
  }

  int period_found = false;
  bool undersized_period_found = false;
  int max_undersized_period = -1;
  
  for (int p = 1; p <= (num_inputs/2); p++) {
    if (((num_inputs % p) == 0) && (((p*scatter_gather_num_tiles) % pipe.consumer_tile_granularity) == 0)) {
      bool p_is_period = true;
      for (int r = 0; r < p; r++) {
        for (int q = p; q < num_inputs; q += p) {
          if (pipe.input_buffer_ids.at(r) != pipe.input_buffer_ids.at(q + r)) {
            p_is_period = false;
            break;
          }
        }
        if (!p_is_period) {
          break;
        }
      }
      if (p_is_period) {
        period_found = true;
        period = p;
        if (p >= MIN_PERIODIC_PIPE_UNROLL) {
          break;
        }
        else {
          undersized_period_found = true;
          max_undersized_period = p;
        }        
      }
    }
  }

  if (period_found) {
    if ((period > MAX_PERIODIC_PIPE_UNROLL) && undersized_period_found) {
      period = max_undersized_period;
    }
    periodic_repeat = num_inputs / period;
  }

  return period_found;
}

template<> struct std::hash<std::vector<uint64_t>> {
    std::size_t operator()(std::vector<uint64_t> const& vec) const noexcept {
        std::size_t ret = 0;
        for(auto& i : vec) {
            ret = ret ^ (std::hash<uint64_t>()(i) << 1);
        }
        return ret;
    }
};

namespace {
std::set<uint64_t> get_unique_queues(const std::vector<uint64_t>& pipe_inputs) {
    std::set<uint64_t> result;
    for(auto b_id : pipe_inputs) {
        uint64_t id = b_id / 100000;
        result.insert(id);
    }
    return result;
}
}

void Net2Pipe::process_dram_fork_pipes(const std::unordered_map<string, tt_op_info>& temporal_epoch_op_map, temporal_epoch_context& epoch_context) const {
    std::unordered_map<uint64_t, std::vector<uint64_t>> dram_buffer_groups;

    // DRAM op groups
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<std::string>>> dram_group_mapping; // [dram_queue_name][op_reader_name][list of grouped ops]

    // for all ops
    for (auto &[op_name, op_info]: temporal_epoch_op_map) {
        // process normal queues and pre-tm prolog queues that dont have forks
        for (auto& input_name : op_info.input_names) {
            const bool not_a_fork = op_info.forked_dram_input_names.find(input_name) == op_info.forked_dram_input_names.end();
            if (not_a_fork and name_is_queue(input_name)) {
                if( dram_group_mapping[input_name].find(op_info.name) == dram_group_mapping[input_name].end()) {
                    dram_group_mapping[input_name][op_info.name] = {};    
                }
            }
        }
        // process the forked_dram_input_names field
        for (auto&[input_queue_name, dram_reader_op] : op_info.forked_dram_input_names ) {
            dram_group_mapping[input_queue_name][dram_reader_op].push_back(op_name);
        }
    }

/*
    std::unordered_map<uint64_t, std::vector<uint64_t>> pipe_groups;
    for (auto &[queue_name, dram_groups] : dram_group_mapping) {
        const tt_queue_info &queue_info = this->parsed_netlist.queue_map.at(queue_name);
        for (int r = 0; r < queue_info.grid_size.r; r++) {
            for (int c = 0; c < queue_info.grid_size.c; c++) {
                std::uint64_t queue_buffer_id = this->queue_name_buf_unique_id_map.at(queue_name).at(r).at(c);
                total_readers_per_buffer
            }
        }
        for (auto &[op_reader_name, op_reader_group] : dram_groups) {
            for (auto & group_member_op_name : op_reader_group) {
            }
        }
    }
*/
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<uint64_t>>> dram_group_pipes; // [dram_queue_name][op_reader_name][list of pipes (ids)]
    for (auto &[pipe_unique_id, pipe] : epoch_context.pipes) {
        const auto op_name = pipe.consumer_name();
        if(temporal_epoch_op_map.find(op_name) == temporal_epoch_op_map.end()){
            continue;
        }
        const auto input_queue_name = temporal_epoch_op_map.at(op_name).input_names[pipe.consumer_input_index()];
        if(dram_group_mapping.find(input_queue_name) == dram_group_mapping.end()) {
            // this is not a fork pipe
            continue;
        }
        // TODO: maybe should handle duplicates here ???
        const auto op_reader_name = dram_group_mapping[input_queue_name].find(op_name) != dram_group_mapping[input_queue_name].end() ?
                                    op_name :
                                    temporal_epoch_op_map.at(op_name).forked_dram_input_names.at(input_queue_name);
                                    
        dram_group_pipes[input_queue_name][op_reader_name].push_back(pipe_unique_id);
    }

    // sort pipes into fork groups and designate first pipe in the list as the dram_reader
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::vector<uint64_t>, std::vector<uint64_t>>>> dram_group_pipes_by_forks;
    for (auto &[input_queue_name, op_readers] : dram_group_pipes) {
        for (auto &[op_reader_name, pipe_ids] : op_readers) {
            std::unordered_map<std::vector<uint64_t>, std::vector<uint64_t>> unique_input_pipes;
            for (auto & pipe_id : pipe_ids) {
                //if(unique_input_pipes.find(p.input_buffer_ids) == unique_input_pipes.end())
                const auto& p = epoch_context.pipes.at(pipe_id);
                const auto op_name = p.consumer_name();
                if (dram_group_mapping[input_queue_name].find(op_name) != dram_group_mapping[input_queue_name].end()) { // this is the dram reader for the group
                    unique_input_pipes[p.input_buffer_ids].insert(unique_input_pipes[p.input_buffer_ids].begin(), pipe_id);
                }
                else {
                    unique_input_pipes[p.input_buffer_ids].push_back(pipe_id);
                }
            }
            dram_group_pipes_by_forks[input_queue_name][op_reader_name] = unique_input_pipes;
        }
    }

    // record num readers per buffer
    std::unordered_map<uint64_t, int> total_readers_per_buffer;
    
    for (auto &[input_queue_name, op_readers] : dram_group_pipes_by_forks) {
        for (auto &[op_reader_name, unique_input_pipes] : op_readers) {
            if(dram_group_mapping.at(input_queue_name).at(op_reader_name).size() == 0) { // TODO: This only works if there is a single dram reader with an internal pipe replica
                // each pipe reader should be separate
                for (auto &[_, pipe_ids] : unique_input_pipes) {
                    for (const auto & dram_reader_pipe_id : pipe_ids) {
                        const auto& dram_reader_pipe = epoch_context.pipes.at(dram_reader_pipe_id);
                        auto unique_queue_ids = get_unique_queues(dram_reader_pipe.input_buffer_ids);
                        for(auto q_id : unique_queue_ids) {
                            total_readers_per_buffer[q_id]++;
                        }
                    }
                }
            }
            else {
                for (auto &[_, pipe_ids] : unique_input_pipes) {
                    const auto& dram_reader_pipe = epoch_context.pipes.at(pipe_ids.at(0));
                    auto unique_queue_ids = get_unique_queues(dram_reader_pipe.input_buffer_ids);
                    for(auto q_id : unique_queue_ids) {
                        total_readers_per_buffer[q_id]++;
                    }
                    /*
                    for(auto b_id : dram_reader_pipe.input_buffer_ids) {
                        total_readers_per_buffer[b_id]++;
                    }*/
                }
            }
        }
    }

    std::unordered_map<uint64_t, int> current_reader_per_buffer;
    for (auto &[input_queue_name, op_readers] : dram_group_pipes_by_forks) {
        for (auto &[op_reader_name, unique_pipe_groups] : op_readers) {
            if(dram_group_mapping.at(input_queue_name).at(op_reader_name).size() == 0) { // TODO: This only works if there is a single dram reader with an internal pipe replica
                for (auto &[_, pipe_ids] : unique_pipe_groups) {
                    const int fork_index = 0;
                    const int num_forks = 1;
                    for (const auto& p_id : pipe_ids) {
                        dram_fork_pipe_metadata pipe_metadata;
                        pipe_metadata.num_forks = num_forks;
                        pipe_metadata.fork_index = fork_index;
                        const auto& forking_pipe = epoch_context.pipes.at(p_id);
                        for(auto buffer_id: forking_pipe.input_buffer_ids) {
                            const int total_readers = total_readers_per_buffer.at(buffer_id/100000);
                            pipe_metadata.total_readers.push_back(total_readers);
                            const int reader_index = current_reader_per_buffer[buffer_id/100000];
                            pipe_metadata.reader_indexes.push_back(reader_index);
                        }

                        epoch_context.dram_fork_pipe_metadata_by_pipe[p_id] = pipe_metadata;
                        auto unique_queue_ids = get_unique_queues(forking_pipe.input_buffer_ids);
                        for(auto q_id : unique_queue_ids) {
                            current_reader_per_buffer[q_id]++;
                        }
                    }                    
                }
            }
            else {
                for (auto &[_, pipe_ids] : unique_pipe_groups) {
                    int fork_index = 0;
                    const int num_forks = pipe_ids.size();
                    for (const auto& p_id : pipe_ids) {
                        dram_fork_pipe_metadata pipe_metadata;
                        pipe_metadata.num_forks = num_forks;
                        pipe_metadata.fork_index = fork_index;
                        const auto& forking_pipe = epoch_context.pipes.at(p_id);
                        for(auto buffer_id: forking_pipe.input_buffer_ids) {
                            const int total_readers = total_readers_per_buffer.at(buffer_id/100000);
                            pipe_metadata.total_readers.push_back(total_readers);
                            const int reader_index = current_reader_per_buffer[buffer_id/100000];
                            pipe_metadata.reader_indexes.push_back(reader_index);
                        }

                        epoch_context.dram_fork_pipe_metadata_by_pipe[p_id] = pipe_metadata;
                        fork_index++;
                    }

                    // get fork index == 0 pipe and increment buffer reader index
                    const auto& dram_reader_pipe = epoch_context.pipes.at(pipe_ids.at(0));
                    auto unique_queue_ids = get_unique_queues(dram_reader_pipe.input_buffer_ids);
                    for(auto q_id : unique_queue_ids) {
                        current_reader_per_buffer[q_id]++;
                    }
                }
            }
        }
    }
}

void Net2Pipe::emit_pipes(YAML::Emitter &out, temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) const {
    std::map<router::unique_id_t, std::vector<router::unique_id_t>> inputs_to_output_pipes_map;
    // Clear this map for each temporal epoch
    epoch_context.input_queue_id_to_consumer_cores.clear();
    epoch_context.output_queue_id_to_producer_cores.clear();

    std::vector<std::uint64_t> grad_op_pipe_ids;
    grad_op_pipe_ids.reserve(epoch_context.grad_op_pipes.size());
    std::sort(grad_op_pipe_ids.begin(), grad_op_pipe_ids.end(), [&](const auto &a, const auto &b) {
        return deterministic_id_map.get_deterministic_key(a) < deterministic_id_map.get_deterministic_key(b);
    });

    for (std::uint64_t pipe_unique_id : grad_op_pipe_ids) {
        // Insert grad op info seperately into inputs_to_outputs_map, since its not included in this -> pipes
        const auto& pipe = epoch_context.grad_op_pipes.at(pipe_unique_id);
        std::vector<router::unique_id_t> input_list_current_pipe;
        std::vector<router::unique_id_t> output_list_current_pipe;
        int period;
        int periodic_repeat;
        if (this->check_pipe_inputs_periodic(pipe, period, periodic_repeat, epoch_context)) {
          std::vector<router::unique_id_t> periodic_input_buffer_ids;
          for (int p = 0; p < period; p++) {
            periodic_input_buffer_ids.push_back(pipe.input_buffer_ids[p]);
          }
          input_list_current_pipe = periodic_input_buffer_ids;
          
        } else {
          input_list_current_pipe = pipe.input_buffer_ids;
        }

        if (!pipe.has_multiple_timesteps()) {
            output_list_current_pipe = pipe.output_buffer_ids();
        } else {
            output_list_current_pipe = flatten_2d_vec<router::unique_id_t>(pipe.time_multiplexed_output_buffer_ids());
        }
        insert_new_input_to_output_entry(inputs_to_output_pipes_map, input_list_current_pipe, {pipe_unique_id});
    }

    for (auto &[pipe_unique_id, pipe] : epoch_context.pipes) {
        std::vector<router::unique_id_t> input_list_current_pipe;
        std::vector<router::unique_id_t> output_list_current_pipe;
        out << YAML::BeginMap;
        out << YAML::Key << ("pipe_" + std::to_string(deterministic_id_map.get_deterministic_key(pipe_unique_id)));
        int op_input_dram_io_buf_size_tiles = 0;
        if (pipe.has_consumer()) {
          std::string dest_name = pipe.consumer_name();
          std::string dest_type;
          if (name_is_op(dest_name, epoch_context)) {
            dest_type = "Op";
            const tt_op_info &op_info = epoch_context.op_info_map.at(dest_name);
            op_input_dram_io_buf_size_tiles = op_info.input_dram_io_buf_size_tiles.at(pipe.consumer_input_index());
          } else {
            dest_type = "Queue";
          }
          out << YAML::Comment(dest_type + ": " + dest_name + ", input " + std::to_string(pipe.consumer_input_index()));
        }
        out << YAML::Value << YAML::BeginMap;

        out << SET_KEY_VAL("id", deterministic_id_map.get_deterministic_key(pipe_unique_id));
        
        bool is_ethernet_pipe = this->is_ethernet_pipe(pipe, epoch_context);
        if (is_ethernet_pipe) {
            out << SET_KEY_VAL("ethernet_pipe", YAML::Flow << 1);
        }

        bool is_downstream;
        bool is_mmio_pipe = this->is_mmio_pipe(pipe, is_downstream, epoch_context);
        if (is_mmio_pipe) {
            out << SET_KEY_VAL("mmio_pipe", 1);
            out << SET_KEY_VAL("mmio_pipe_downstream", (is_downstream ? 1 : 0));
        }
        int period;
        int periodic_repeat;
        if (this->check_pipe_inputs_periodic(pipe, period, periodic_repeat, epoch_context)) {
          std::vector<router::unique_id_t> periodic_input_buffer_ids;
          for (int p = 0; p < period; p++) {
            periodic_input_buffer_ids.push_back(pipe.input_buffer_ids[p]);
          }
          input_list_current_pipe = periodic_input_buffer_ids;

          std::vector<router::unique_id_t> deterministic_input_buffer_ids;
          for (const auto& id : input_list_current_pipe) {
            deterministic_input_buffer_ids.emplace_back(deterministic_id_map.get_deterministic_key(id));
          }
          out << SET_KEY_VAL("input_list", YAML::Flow << deterministic_input_buffer_ids);
          out << SET_KEY_VAL("pipe_periodic_repeat", periodic_repeat);
        } else {
          input_list_current_pipe = pipe.input_buffer_ids;
          std::vector<router::unique_id_t> deterministic_input_buffer_ids;
          for (const auto& id : input_list_current_pipe) {
            deterministic_input_buffer_ids.emplace_back(deterministic_id_map.get_deterministic_key(id));
          }
          out << SET_KEY_VAL("input_list", YAML::Flow << deterministic_input_buffer_ids);
          out << SET_KEY_VAL("pipe_periodic_repeat", 0);
        }

        int pipe_consumer_repeat = this->check_pipe_consumer_repeat(pipe);
        out << SET_KEY_VAL("pipe_consumer_repeat", pipe_consumer_repeat);

        if (!pipe.has_multiple_timesteps()) {
            output_list_current_pipe = pipe.output_buffer_ids();
            std::vector<router::unique_id_t> deterministic_output_buffer_ids;
            for (const auto& id : output_list_current_pipe) {
                deterministic_output_buffer_ids.emplace_back(deterministic_id_map.get_deterministic_key(id));
            }
            out << SET_KEY_VAL("output_list", YAML::Flow << deterministic_output_buffer_ids);
        } else {
            auto deterministic_output_buffer_ids = pipe.time_multiplexed_output_buffer_ids();
            for (auto& v : deterministic_output_buffer_ids) {
                for (auto& id : v) {
                    id = deterministic_id_map.get_deterministic_key(id);
                }
            }
            out << SET_KEY_VAL("output_list", YAML::Flow << deterministic_output_buffer_ids);
            output_list_current_pipe = flatten_2d_vec<router::unique_id_t>(pipe.time_multiplexed_output_buffer_ids());
        }

        out << SET_KEY_VAL("incoming_noc_id", YAML::Flow << pipe.incoming_noc_id);
        out << SET_KEY_VAL("outgoing_noc_id", YAML::Flow << pipe.outgoing_noc_id);
        out << SET_KEY_VAL("outgoing_vc", YAML::Flow << pipe.outgoing_vc);
        out << SET_KEY_VAL("incoming_vc", YAML::Flow << pipe.incoming_vc);
        if (op_input_dram_io_buf_size_tiles != 0) {
          out << SET_KEY_VAL("op_input_dram_io_buf_size_tiles", YAML::Flow << op_input_dram_io_buf_size_tiles);
        }
        insert_new_input_to_output_entry(inputs_to_output_pipes_map, input_list_current_pipe, {pipe_unique_id});

        if (pipe.is_direct_mcast()) {
            out << SET_KEY_VAL("direct_mcast", 1);
        }

        if (!pipe.is_scatter()) {
            int ethernet_chan = -1;
            int y = pipe.core_location().y;
            int x = pipe.core_location().x;
            chip_id_t chip = pipe.core_location().chip;
            auto const& soc_desc = this->soc_descriptors.at(chip);
            if (not is_ethernet_pipe) {
                if (soc_desc.routing_y_to_worker_y.find(y) != soc_desc.routing_y_to_worker_y.end() &&
                    soc_desc.routing_x_to_worker_x.find(x) != soc_desc.routing_x_to_worker_x.end()) {
                    y = soc_desc.routing_y_to_worker_y.at(y);
                    x = soc_desc.routing_x_to_worker_x.at(x);
                } else {
                    // Technically this is a bug. In these cases where the pipe location is a routing location that doesn't map to a worker core
                    // pipegen won't have any way to know it's dealing with a routing location. Since we only expect this scenario where the pipe location
                    // is on an ethernet core, we set an ethernet channel attribute. Revisit this in the future for a more generalized approach.
                    //
                    // This pops up (especially) when we have queue buffer -> ethernet core pipes (i.e. read queue buffer to remote chip, over ethernet)
                    // TT_ASSERT(pipe.input_buffer_ids.size() == 1 && (!pipe.has_multiple_timesteps() && pipe.output_buffer_ids().size() == 1));

                    TT_ASSERT(soc_desc.is_ethernet_core(tt_xy_pair(x,y)));
                    ethernet_chan = soc_desc.get_channel_of_ethernet_core(tt_xy_pair(x,y));
                }
            }
            if (ethernet_chan >= 0) {
                out << SET_KEY_VAL("ethernet_chan", ethernet_chan);
            }
            out << SET_KEY_VAL("mcast_core_rc", SET_COORD3(pipe.core_location().chip, y, x));
        } else {
            std::vector<std::vector<int>> pipe_coords_with_chip_id(pipe.core_locations().size());

            const tt_xy_pair &first_consumer_core_xy = tt_xy_pair(pipe.core_locations().at(0));
            auto const& soc_desc = this->soc_descriptors.at(pipe.core_locations().at(0).chip);
            bool consumer_is_worker_core = soc_desc.is_worker_core(first_consumer_core_xy);
            TT_ASSERT(consumer_is_worker_core || soc_desc.is_ethernet_core(first_consumer_core_xy));
            int ethernet_chan = consumer_is_worker_core ? -1 : soc_desc.get_channel_of_ethernet_core(first_consumer_core_xy);

            int p = 0;
            for (const auto &it : pipe.core_locations()) {
                const tt_xy_pair &core_xy = tt_xy_pair(it.x, it.y);
                bool is_worker_core = soc_desc.is_worker_core(core_xy);
                TT_ASSERT(consumer_is_worker_core == is_worker_core, "Net2pipe doesn't currently support pipes that scatter to a mix of tensix and non-tensix cores");
                
                const tt_cxy_pair &mcast_core = tt_cxy_pair(
                    it.chip, 
                    is_worker_core ? soc_desc.get_worker_core(core_xy) : core_xy
                );

                if (!is_worker_core) {
                    TT_ASSERT(soc_desc.is_ethernet_core(core_xy));
                    // In order to support this we first need to update the pipegen yaml syntax to support multiple ethernet channels, corresponding to the multiple mcast_core_rcs
                    // Technically this is actually fine since pipegen doesn't use the eth channel value directly for the pipe - it only uses its presence to determine if the mcast_core_rc
                    // is a routing coord directly (instead of worker coord that needs mapping to routing coords)
                    TT_ASSERT(ethernet_chan == soc_desc.get_channel_of_ethernet_core(core_xy), "Net2pipe doesn't currently support pipes that scatter to multiple different ethernet cores");
                }

                pipe_coords_with_chip_id[p].push_back(mcast_core.chip);
                pipe_coords_with_chip_id[p].push_back(mcast_core.y);
                pipe_coords_with_chip_id[p].push_back(mcast_core.x);
                p++;
            }
            if (ethernet_chan >= 0) {
                out << SET_KEY_VAL("ethernet_chan", ethernet_chan);
            }
            out << SET_KEY_VAL("mcast_core_rc", YAML::Flow << pipe_coords_with_chip_id);
            out << SET_KEY_VAL("output_padding_list", YAML::Flow << pipe.output_padding_buffer_list);
        }
        if(epoch_context.dram_fork_pipe_metadata_by_pipe.find(pipe_unique_id) != epoch_context.dram_fork_pipe_metadata_by_pipe.end()) {
            const auto& metadata = epoch_context.dram_fork_pipe_metadata_by_pipe.at(pipe_unique_id);

            out << SET_KEY_VAL("dram_pipe_total_readers", YAML::Flow << metadata.total_readers);
            out << SET_KEY_VAL("dram_pipe_reader_index", YAML::Flow << metadata.reader_indexes);
            out << SET_KEY_VAL("dram_pipe_num_forks", metadata.num_forks);
            out << SET_KEY_VAL("dram_pipe_fork_index", metadata.fork_index);
        }
        out << YAML::EndMap;
        out << YAML::EndMap;
    }
    get_queue_consumer_map(inputs_to_output_pipes_map, epoch_context);
    get_queue_producer_map(inputs_to_output_pipes_map, epoch_context);
}


int Net2Pipe::get_dram_prefetch_noc_id(const std::string& queue_name, const std::string& op_name) const {
  return 0; // FIXME imatosevic - do we need to optimize this ever? 
}


void Net2Pipe::get_op_core_routing_cxy(const temporal_epoch_context& epoch_context, const std::string &op_name, int r_logical, int c_logical, int &y_coord, int &x_coord) const {
    tt_op_info const& op_info = epoch_context.op_info_map.at(op_name);
    bool is_ethernet_datacopy = netlist_utils::is_valid_ethernet_op(op_info.type);
    chip_id_t chip = this->op_graph_map.at(op_name).target_device;
    if (is_ethernet_datacopy) {
        int dest_channel = op_info.attributes.ethernet_datacopy_attr.egress_channels.at(r_logical * op_info.grid_size.c + c_logical);
        const tt_xy_pair channel_xy = this->soc_descriptors.at(chip).ethernet_cores.at(dest_channel);
        y_coord = channel_xy.y;
        x_coord = channel_xy.x;
    } else {
        op_info.get_core_yx_coord(r_logical, c_logical, y_coord, x_coord);
        x_coord = soc_descriptors.at(chip).worker_log_to_routing_x.at(x_coord);
        y_coord = soc_descriptors.at(chip).worker_log_to_routing_y.at(y_coord);
    }
};  

void Net2Pipe::get_op_input_pipe_mcast_core_rc(
    const tt_op_info &op_info,
    const temporal_epoch_context& epoch_context,
    int input_idx,
    const phase_pipe_tile_map &pipe,
    const std::vector<std::vector<std::uint64_t>> &pipe_outputs,
    std::vector<std::tuple<chip_id_t, int, int>> &pipe_coords,
    bool &pipe_coords_are_ethernet_channels) const {

    std::string input_name = op_info.input_names.at(input_idx);
    log_debug(tt::LogNet2Pipe,
            "input_pipe_mcast_core for op '{}' input {} '{}'", op_info.name, input_idx, op_info.input_names[input_idx]
        );
    bool all_inputs_on_same_core = false;
    int last_input_core_r = -1;
    int last_input_core_c = -1;
    const bool input_is_prolog_tm =
        epoch_context.prolog_post_tm_operand.find(op_info.name) != epoch_context.prolog_post_tm_operand.end() &&
        epoch_context.prolog_post_tm_operand.at(op_info.name).find(input_name) !=
            epoch_context.prolog_post_tm_operand.at(op_info.name).end() && epoch_context.prolog_post_tm_operand.at(op_info.name).at(input_name);

    const bool queue_is_post_tm_prolog = name_is_queue(input_name) and epoch_context.queue_setting_map.at(input_name).prolog and input_is_prolog_tm;
    log_debug(tt::LogNet2Pipe,
            "queue_is_post_tm_prolog = {}", queue_is_post_tm_prolog
        );
    bool valid_scatter_op = name_is_op(input_name, epoch_context) || (name_is_queue(input_name) && ((epoch_context.queue_setting_map.at(input_name).prolog and not input_is_prolog_tm) || epoch_context.queue_setting_map.at(input_name).epilog));
    
    bool scatter_pipe = pipe_outputs.size() > 1;
    bool scatter_pipe_with_duplicates = false;
    if (scatter_pipe) {
        scatter_pipe_with_duplicates = 1 < pipe_scatter_outputs_consumer_repeat(pipe_outputs, pipe.padding_output_list);
        int num_scatter_steps = pipe_outputs.size();
        // Would be better if we could hash every scatter segment
        // -> could hash each entry once, do a lookup, if it doesn't exist, store it and proceed, else implies duplicate
        for (int step = 0; step < num_scatter_steps && !scatter_pipe_with_duplicates; step++) {
            for (int step_other = step + 1; step_other < num_scatter_steps && !scatter_pipe_with_duplicates; step_other++) {
                scatter_pipe_with_duplicates = scatter_pipe_with_duplicates && (pipe_outputs.at(step) == pipe_outputs.at(step_other));
            }
        }
    }
    
    const bool is_prolog = is_name_prolog_queue(input_name, epoch_context);

    if (valid_scatter_op) {
        all_inputs_on_same_core = true;

        for (tile_to_core_index_map it : pipe.tile_map) {
            // Wrap inputs onto the local core grid if the input is_prolog
            int input_core_r = is_prolog ? it.core_r % op_info.grid_size_logical_r() : it.core_r;
            int input_core_c = is_prolog ? it.core_c % op_info.grid_size_logical_c() : it.core_c;

            if (last_input_core_r == -1) {
                last_input_core_r = input_core_r;
                last_input_core_c = input_core_c;
            } else if (last_input_core_r != input_core_r || last_input_core_c != input_core_c) {
                all_inputs_on_same_core = false;
                break;
            }
        }
    }

    bool x_mcast_noc1 = false;
    bool y_mcast_noc1 = true;
    
    bool col_mcast_noc1 = op_info.grid_transpose ? x_mcast_noc1 : y_mcast_noc1;
    bool row_mcast_noc1 = op_info.grid_transpose ? y_mcast_noc1 : x_mcast_noc1;
   
    bool op_input_row_mcast;
    bool op_input_col_mcast;
    bool op_input_noc1_mcast;
    n2p::get_op_input_mcast(op_info, input_idx, epoch_context.fused_op_schedule_map, op_input_row_mcast, op_input_col_mcast, op_input_noc1_mcast);

    chip_id_t producer_chip =
        name_is_op(input_name, epoch_context)
            ? this->op_graph_map.at(input_name).target_device
            : epoch_context.queue_unique_id_info_map.at(epoch_context.queue_name_unique_id_map.at(input_name)).target_device;
    chip_id_t consumer_chip = this->op_graph_map.at(op_info.name).target_device;

    bool adjacent_mcast;
    int adjacent_noc_id;
    adjacent_mcast = is_input_adjacent_mcast(input_name, op_info.name, adjacent_noc_id, epoch_context);

    // dest cores are the cores, starting at [0,0], within the op-grid, independent of where the op is placed on the chip
    for (auto it : pipe.dest_cores) {
        TT_ASSERT((it.first != -1) || op_input_col_mcast);
        TT_ASSERT((it.second != -1) || op_input_row_mcast);
        bool col_mcast_pipe = (it.first == -1) && (op_info.grid_size_logical_r() > 1);
        bool row_mcast_pipe = (it.second == -1) && (op_info.grid_size_logical_c() > 1);
        bool mcast_pipe = col_mcast_pipe || row_mcast_pipe;
        int dest_core_r;
        int dest_core_c;
        if (col_mcast_pipe) {
          dest_core_r = op_input_noc1_mcast ? (op_info.grid_size_logical_r() - 1) : 0;
        } else {
          dest_core_r = (it.first == -1) ? 0 : it.first;
        }
        if (row_mcast_pipe) {
          dest_core_c = op_input_noc1_mcast ? (op_info.grid_size_logical_c() - 1) : 0;
        } else {
          dest_core_c = (it.second == -1) ? 0 : it.second;
        }
        bool direct_mcast = adjacent_mcast && !scatter_pipe && all_inputs_on_same_core;
        int pipe_y, pipe_x;
        chip_id_t pipe_chip;
        bool producer_side_unicast_pipe = 
            !mcast_pipe &&
            !scatter_pipe_with_duplicates &&
            valid_scatter_op && 
            all_inputs_on_same_core;

        bool use_last_input_pipe_coords = producer_side_unicast_pipe || direct_mcast;

        if (use_last_input_pipe_coords) {
            pipe_chip = producer_chip;
            if (name_is_op(input_name, epoch_context)) {
              const auto &input_op_info = epoch_context.op_info_map.at(input_name);
              if (netlist_utils::is_valid_ethernet_op(input_op_info.type)) {
                pipe_chip = consumer_chip;
                // if it's an ethernet op, we need to get the core from the output, which is the core on the other side of the
                // link connected to the core the op is placed on
                int egress_channel = last_input_core_r * input_op_info.grid_size.c + last_input_core_c;
                int ethernet_channel = input_op_info.attributes.ethernet_datacopy_attr.egress_channels.at(egress_channel);
                TT_ASSERT(last_input_core_r * input_op_info.grid_size.c + last_input_core_c < input_op_info.grid_size.c * input_op_info.grid_size.r);
                const tt_xy_pair &eth_datacopy_egress_core = this->soc_descriptors.at(pipe_chip).ethernet_cores.at(ethernet_channel);
                pipe_y = eth_datacopy_egress_core.y;
                pipe_x = eth_datacopy_egress_core.x;

                pipe_coords_are_ethernet_channels = true;
              } else {
                epoch_context.op_info_map.at(input_name).get_core_yx_coord(last_input_core_r, last_input_core_c, pipe_y, pipe_x);
              }
            } else {
              op_info.get_core_yx_coord(last_input_core_r, last_input_core_c, pipe_y, pipe_x);
            }
        } else {
            pipe_chip = consumer_chip;
            if (netlist_utils::is_valid_ethernet_op(op_info.type)) {
                pipe_y = dest_core_r;
                pipe_x = dest_core_c;
                int egress_channel = dest_core_r * op_info.grid_size.c + dest_core_c;
                int ethernet_channel = op_info.attributes.ethernet_datacopy_attr.ingress_channels.at(egress_channel);
                TT_ASSERT(dest_core_r * op_info.grid_size.c + dest_core_c < op_info.grid_size.c * op_info.grid_size.r);
                const tt_xy_pair &eth_datacopy_ingress_core = this->soc_descriptors.at(pipe_chip).ethernet_cores.at(ethernet_channel);
                pipe_y = eth_datacopy_ingress_core.y;
                pipe_x = eth_datacopy_ingress_core.x;
                pipe_coords_are_ethernet_channels = true;
            } else {
                op_info.get_core_yx_coord(dest_core_r, dest_core_c, pipe_y, pipe_x);
            }
        }
        log_debug(tt::LogNet2Pipe,
            "mcast_pipe={}, valid_scatter_op={}, last_input_core = ({}, {}), pipecoord = ({}, {})", mcast_pipe, valid_scatter_op, last_input_core_r, last_input_core_c, pipe_y, pipe_x
        );
        pipe_coords.push_back(std::make_tuple(pipe_chip, pipe_y, pipe_x));
    }
}

void Net2Pipe::register_pipe_as_output_of_buffers(
    std::uint64_t pipe_id, const std::vector<std::uint64_t> &buffer_ids, temporal_epoch_context& epoch_context) const {
    for (auto id : buffer_ids) {
        epoch_context.buffer_output_pipes[id].insert(pipe_id);
    }
}

void Net2Pipe::register_pipe_as_input_of_buffers(
    std::uint64_t pipe_id, const std::vector<std::uint64_t> &buffer_ids, temporal_epoch_context& epoch_context) const {
    for (auto id : buffer_ids) {
        epoch_context.buffer_input_pipes.insert({id, pipe_id});
    }
}

void Net2Pipe::register_pipe_as_input_of_buffers(
    std::uint64_t pipe_id, const time_multiplexed_outputs_t &timestep_buffer_ids, temporal_epoch_context& epoch_context) const {
    for (const auto &buffer_ids : timestep_buffer_ids) {
        for (auto id : buffer_ids) {
            epoch_context.buffer_input_pipes.insert({id, pipe_id});
        }
    }
}

std::tuple<
    std::unordered_map<std::uint64_t, tt_queue_info>,
    std::map<std::uint64_t, router::router_buffer_info_t>,
    std::map<std::uint64_t, pipe_t>,
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>>,
    std::unordered_map<std::uint64_t, std::uint64_t>,
    std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>>,
    std::map<std::string, std::map<int, std::map<int, std::map<int, std::uint64_t>>>>,
    std::map<std::string, std::map<int, std::map<int, std::uint64_t>>>>
Net2Pipe::export_router_to_net2pipe(const router::Router &router) const {
    return {
        router.get_queue_map(),
        router.get_buffer_map(),
        router.get_pipes(),
        router.get_buffer_output_pipes(),
        router.get_buffer_input_pipes(),
        router.get_op_input_buf_map(),
        router.get_op_intermediate_buf_map(),
        router.get_op_output_buf_map()};
}


void Net2Pipe::run_router(int temporal_epoch, temporal_epoch_context& epoch_context, ClusterDescription cluster_description) const {

    std::vector<chip_id_t> temporal_epoch_chip_ids = {};
    std::transform(
        temporal_epoch_graph_exec_vars.at(temporal_epoch).begin(),
        temporal_epoch_graph_exec_vars.at(temporal_epoch).end(),
        std::back_inserter(temporal_epoch_chip_ids),
        [this](const auto &graph_exec_var) {
            return this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name).target_device;
        }
    );

    log_debug(tt::LogRouter, "{}", "temporal_epoch_chip_ids");
    for (auto id : temporal_epoch_chip_ids) {
        log_debug(tt::LogRouter, "\t{}", id);
    }

    if (this->config.is_feature_ethernet_multichip_compile_enabled) {
        cluster_description.specify_enabled_devices(temporal_epoch_chip_ids);
    }
    
    log_debug(tt::LogNet2Pipe, "---------------------------");
    log_debug(tt::LogNet2Pipe, "Routing temporal epoch {}", temporal_epoch);
    log_debug(tt::LogNet2Pipe, "---------------------------");
    log_trace(tt::LogRouter, "BEFORE ROUTER");

    for (const auto &[id, buffer_info] : epoch_context.buffer_map) {
        log_trace(tt::LogRouter, "\t{}", id);
    }

    auto chip_set = cluster_description.get_all_chips();
    std::vector<chip_id_t> chip_ids(chip_set.begin(), chip_set.end());
    TT_ASSERT(chip_ids.size() > 0);
    TT_ASSERT(this->config.is_feature_ethernet_multichip_compile_enabled || epoch_context.buffer_map.size() > 0);
    TT_ASSERT(this->config.is_feature_ethernet_multichip_compile_enabled || epoch_context.pipes.size() > 0);
    for (const auto &[id, buf] : epoch_context.buffer_map) {
        if (!buf.is_queue()) {
            if (buf.info().type() == RouterBufferType::Input) {
                TT_ASSERT(epoch_context.buffer_input_pipes.find(id) != epoch_context.buffer_input_pipes.end());
            }
            if (buf.info().type() == RouterBufferType::Output) {
                // Gradient op output buffers don't have this limit.
                // TT_ASSERT(buffer_output_pipes.find(id) != buffer_output_pipes.end());
            }
        }
    }
    log_trace(tt::LogRouter, "Validated buffer_input_pipes and buffer_output_pipes");
    log_trace(tt::LogRouter, "buffer_input_pipes: ");
    for (const auto &[buf_id, input_pipe_id] : epoch_context.buffer_input_pipes) {
        log_trace(tt::LogRouter, "\tpipe {} -> buf_id {} ", input_pipe_id, buf_id);
    }
    log_trace(tt::LogRouter, "buffer_output_pipes: ");
    for (const auto &[buf_id, output_pipe_ids] : epoch_context.buffer_output_pipes) {
        log_trace(tt::LogRouter, "\tbuf_id {} -> pipes: ", buf_id);
        for (const auto p_id : output_pipe_ids) {
            log_trace(tt::LogRouter, "\t\t{}", p_id);
        }
    }

    // Need this info for pipes generated inside Router:
    validate::unique_id_range_covers_all_buffers(epoch_context.buffer_map, epoch_context.queue_unique_id_info_map);

    auto router = router::Router(
        this->soc_descriptors,
        chip_ids,
        ClusterGraph(cluster_description),

        n2p::UNIQUE_ID_ALIGN,
        1,
        this->unique_id_gen,

        epoch_context.queue_unique_id_info_map,
        epoch_context.buffer_map,
        epoch_context.pipes,
        epoch_context.buffer_output_pipes,
        epoch_context.buffer_input_pipes,
        epoch_context.op_input_buf_map,
        epoch_context.op_intermediate_buf_map,
        epoch_context.op_output_buf_map,
        epoch_context.fused_op_schedule_map,

        epoch_context.op_queue_output_scatter,
        epoch_context.op_queue_output_buf_granularity,
        epoch_context.op_input_kernel_tile_clear_granularity,

        epoch_context.op_info_map,
        epoch_context.queue_setting_map,
        epoch_context.op_input_name_map,
        epoch_context.prolog_buffers_per_op,
        epoch_context.prolog_post_tm_operand,
        epoch_context.prolog_queue_name_fork_buf_unique_id_map,
        epoch_context.op_queue_output_map,
        epoch_context.horonological_unique_keys);

    router.run(this->parsed_netlist);
    const auto
        &[_queue_unique_id_info_map,
          _buffer_map,
          _pipes,
          _buffer_output_pipes,
          _buffer_input_pipes,
          _op_input_buf_map,
          _op_intermediate_buf_map,
          _op_output_buf_map] = this->export_router_to_net2pipe(router);

    epoch_context.buffer_map = _buffer_map;
    epoch_context.pipes = _pipes;
    epoch_context.buffer_output_pipes = _buffer_output_pipes;
    epoch_context.buffer_input_pipes = _buffer_input_pipes;
    epoch_context.op_input_buf_map = _op_input_buf_map;
    epoch_context.op_intermediate_buf_map = _op_intermediate_buf_map;
    epoch_context.op_output_buf_map = _op_output_buf_map;


    log_trace(tt::LogRouter, "AFTER ROUTER");
    for (const auto &[id, buffer_info] : epoch_context.buffer_map) {
        log_trace(tt::LogRouter, "\t{}", id);
    }

    validate::unique_id_range_covers_all_buffers(epoch_context.buffer_map, epoch_context.queue_unique_id_info_map);
    this->post_router_validate_all_pipes_placed_on_same_chip_as_inputs_and_outputs(epoch_context);
    log_debug(tt::LogNet2Pipe, "---------------------------");
}


// Get all target_device_ids from graphs, queues and set the starting/ending (lowest/highest) target_device_id for ring topology.
void Net2Pipe::set_ring_start_end_device_ids() {

    chip_id_t starting_device_id = -1;
    chip_id_t ending_device_id   = -1;

    if (this->config.arch != tt::ARCH::WORMHOLE && this->config.arch != tt::ARCH::WORMHOLE_B0) {

        int num_enabled_devices = workload_target_device_ids.size();
        starting_device_id     = *workload_target_device_ids.begin();
        ending_device_id       = *workload_target_device_ids.rbegin();
        TT_ASSERT(num_enabled_devices > 0);

        // Requirement for ring topology in GS: Only sequential, no gaps in logical target_device_id's specified by workload.
        if ((starting_device_id + num_enabled_devices - 1) != ending_device_id){
            log_fatal("The set of workload target_device_id's must be sequential, without gaps for ring topology.");
        }

    }

    this->starting_device_id = starting_device_id;
    this->ending_device_id = ending_device_id;

    log_debug(tt::LogNet2Pipe, "set_ring_start_end_device_ids() computed starting_device_id: {} ending_device_id: {} from netlist.", this->starting_device_id, this->ending_device_id);

}

uint64_t Net2Pipe::get_pad_buffer_id(const tt_op_info& op_info, temporal_epoch_context& epoch_context, const int input_index, const int chip, const int core_y, const int core_x) const {

    const std::string op_name = op_info.name;
    const n2p::padding_db_key key = {op_name, input_index, chip, core_y, core_x};
    const std::string& input_name = epoch_context.op_input_name_map.at(op_name).at(input_index);
    
    // If we already created the buffer, just return the ID
    if(epoch_context.pad_buffers_db.find(key) != epoch_context.pad_buffers_db.end()) {
        if( not (name_is_queue(input_name) and not epoch_context.queue_setting_map.at(input_name).prolog)) {
            auto & buf = epoch_context.pad_buffers_db.at(key);
            buf.tiles_per_input += buf.scatter_gather_num_tiles;
        }
        return epoch_context.pad_buffers_db.at(key).uniqid;
    }
    
    n2p::padding_buffer buf;
    const DataFormat data_format = op_info.input_data_formats.at(input_index);
    const TileDim tile_dim = op_info.input_tile_dims.at(input_index);
    const float pad_value = op_info.input_padding.at(input_index).pad_value;
    if (name_is_queue(input_name) and not epoch_context.queue_setting_map.at(input_name).prolog) {
        bool dummy_is_scatter;
        const tt_queue_info &queue_info = this->parsed_netlist.queue_map.at(input_name);
        this->get_queue_attributes(
            queue_info,
            epoch_context.input_count,
            false,  // is_prolog,
            epoch_context,
            buf.size_tiles,
            buf.tiles_per_input, //tiles_per_input,
            buf.tile_size, //tile_size_bytes,
            buf.epoch_tiles, //buf_epoch_tiles,
            buf.replicate, //replicate
            dummy_is_scatter
        );
        buf.type = n2p::padding_buffer::padding_buffer_type::DRAM_IO;
        buf.comment = "Padding Buffer: DRAM";
        buf.md_op_name = op_name;
        buf.operand_name = "Padding";
        buf.uniqid = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
        buf.id = 0; // TODO: check
        // TODO: may need to move this assignment or calculation to earlier in the process
        buf.scatter_gather_num_tiles = epoch_context.op_queue_output_buf_granularity.at(input_name);

        buf.chip_id = chip;
        buf.core_r = 255;
        buf.core_c = 255;
        buf.dram_io_flag = 1;
        buf.dram_buf_flag = 0;
        buf.dram_chan = 0; // TODO: Optimize
        buf.dram_addr = this->get_dram_pad_addr(data_format, pad_value);
        buf.q_slots = queue_info.entries;
        buf.prefetch_type = -1;
        epoch_context.pad_buffers_db.insert({key, buf});

        const bool prolog = false;
        const auto& buf_info = router::router_buffer_info_t(router::buffer_queue_info_t(router::NO_ASSOCIATED_QUEUE_OBJECT_ID, router::router_queue_allocation_info{buf.dram_chan, buf.dram_addr, queue_info.loc==QUEUE_LOCATION::HOST}, prolog), chip);
        epoch_context.buffer_map.insert({buf.uniqid, buf_info});
        epoch_context.buffer_op_or_queue_name_map.insert({buf.uniqid, input_name});

    } else {
        const bool post_tm_prolog = epoch_context.prolog_post_tm_operand.find(op_name) != epoch_context.prolog_post_tm_operand.end() and
                                         epoch_context.prolog_post_tm_operand.at(op_name).find(input_name) != epoch_context.prolog_post_tm_operand.at(op_name).end() and
                                         epoch_context.prolog_post_tm_operand.at(op_name).at(input_name);
        const bool post_tm = name_is_queue(input_name) and
                             epoch_context.queue_setting_map.at(input_name).prolog and
                             post_tm_prolog;
        
        int epoch_tiles = -1;
        if(name_is_queue(input_name)) {
            epoch_tiles = epoch_context.input_count * epoch_context.op_queue_output_buf_granularity.at(input_name);
        }
        else {
            auto input_op_info = epoch_context.op_info_map.at(input_name);
            auto output_single_buf_size_mblocks = n2p::get_op_output_single_buf_size_mblocks(input_op_info);
            auto scatter_gather_num_tiles = epoch_context.op_queue_output_buf_granularity.at(input_name);
            if (epoch_context.input_count * input_op_info.output_dim.t <= output_single_buf_size_mblocks) { //truncated buffer
                epoch_tiles = scatter_gather_num_tiles; //this->input_count * scatter_gather_num_tiles;
            }
            else {
                epoch_tiles = epoch_context.input_count * scatter_gather_num_tiles * input_op_info.output_dim.t  / output_single_buf_size_mblocks;
            }

            TT_ASSERT(epoch_tiles > 0, "input_op_info.output_dim.t: {}, this->op_queue_output_buf_granularity.at(input_name): {}, output_single_buf_size_mblocks: {}", input_op_info.output_dim.t, epoch_context.op_queue_output_buf_granularity.at(input_name), output_single_buf_size_mblocks);
        }


        buf.comment = post_tm ? "Padding Buffer: L1 Post-TM" : "Padding Buffer: L1 Pre-TM";
        buf.md_op_name = op_name;
        buf.operand_name = "Padding";
        buf.uniqid = get_next_unique_id(epoch_context.horonological_unique_keys, n2p::UNIQUE_ID_ALIGN);
        buf.id = 0; // CHECK
        buf.epoch_tiles = epoch_tiles; // TODO ?
        buf.scatter_gather_num_tiles = epoch_context.op_queue_output_buf_granularity.at(input_name);
        buf.tiles_per_input = buf.scatter_gather_num_tiles;
        buf.size_tiles = buf.scatter_gather_num_tiles;
        buf.replicate = 1;
        buf.tile_size = n2p::get_format_tile_size_bytes(data_format, true, tile_dim); // TODO ?
        buf.chip_id = chip;
        buf.core_r = core_y;
        buf.core_c = core_x;
        buf.dram_io_flag = 0;
        buf.dram_buf_flag = 1;
        buf.dram_chan = 0; // TODO: OPTIMIZE
        buf.dram_addr = this->get_dram_pad_addr(data_format, pad_value);
        buf.q_slots = 1;
        buf.prefetch_type = post_tm ? 1 : 0;
        epoch_context.pad_buffers_db.insert({key, buf});
        const bool prolog = true;
        auto bqi = router::buffer_queue_info_t(router::NO_ASSOCIATED_QUEUE_OBJECT_ID, router::router_queue_allocation_info{buf.dram_chan, buf.dram_addr, false}, prolog);
        bqi.set_prolog_core_location(tt_xy_pair(core_x, core_y));
        const auto& buf_info = router::router_buffer_info_t(bqi, chip);
        epoch_context.buffer_map.insert({buf.uniqid, buf_info});
    }

    // Create queue also; for router flow
    const std::string queue_name = "padding_" + to_string(std::hash<n2p::padding_db_key>{}(key));
    tt_queue_info q_info = {
        .name = queue_name,
        .input = "",
        .target_device = chip,
        .entries = 1,
        .grid_size = {1, 1},
        .input_count = 1,
        .data_format = data_format,
        .type = IO_TYPE::Queue,
        .loc = QUEUE_LOCATION::DRAM,
        .alloc_info = {{buf.dram_chan, buf.dram_addr}},
        .layout = IO_LAYOUT::Tilized,
    };

    epoch_context.queue_unique_id_info_map.insert({buf.uniqid, q_info});
    epoch_context.queue_name_unique_id_map.insert({queue_name, buf.uniqid});

    epoch_context.op_queue_output_scatter.insert({queue_name, false});

    return buf.uniqid;
}

// Creates padding_table.yaml in the output dir that contains a
// description of the tiles that should be populated in the padding section of dram
void Net2Pipe::emit_padding_table() {
    // yaml-cpp does not serialize -.inf or .inf correctly so we must do it manually here
    auto float_to_yaml = [](float f) {
        stringstream ss;;
        if(isinf(f)) {
            if (f < 0) {
                ss << "-.inf";
            }
            else {
                ss << ".inf";
            }
        }
        else {
            ss << f;
        }
        return ss.str();
    };

    YAML::Emitter out_yaml;
    out_yaml << YAML::BeginSeq;
    for(const auto& [df, t]: this->dram_pad_addr_table) {
        for(const auto& [pad_val, addr]: t) {
            // TODO: if we start running out of space in the table, may need to do smarter per chip and channel allocations 
            // At best we can specialize to: {chip, channel, address, num_tiles, dataformat, value}
            out_yaml << YAML::BeginMap;
            out_yaml << YAML::Key << "address";
            out_yaml << YAML::Value << addr;
            out_yaml << YAML::Key << "data_format";
            out_yaml << YAML::Value << static_cast<uint32_t>(df);
            out_yaml << YAML::Key << "padding_value";
            out_yaml << YAML::Value << float_to_yaml(pad_val);
            out_yaml << YAML::Key << "size_tiles";
            out_yaml << YAML::Value << MAX_PADDING_SCATTER_TILES;
            out_yaml << YAML::EndMap;
        }
    }
    out_yaml << YAML::EndSeq;

    const std::string cmd = "mkdir -p " + this->output_dir;
    const int status = system(cmd.c_str());
    //n2p::Log() << "return value=" << status << std::endl;

    std::ofstream out_file;
    const std::string yaml_out_file_path = this->output_dir + "/padding_table.yaml";
    //n2p::Log() << "Writing queue yaml file: " << queues_yaml_out_file_path << "\n";
    out_file.open(yaml_out_file_path);
    out_file << out_yaml.c_str();
    out_file.close();
}

void Net2Pipe::build_padding_table() {
    const uint32_t start_addr = dram_mem::address_map::DRAM_EACH_BANK_CONST_BLOB_ADDR;
    const uint32_t blob_size  = dram_mem::address_map::DRAM_EACH_BANK_CONST_BLOB_SIZE;
    uint32_t addr_offset = 0;

    size_t num_temporal_epochs = this->temporal_epoch_graph_exec_vars.size();
    // for all temporal epochs
    for (size_t temporal_epoch = 0; temporal_epoch < num_temporal_epochs; temporal_epoch++) {
        const auto &graph_exec_vars = this->temporal_epoch_graph_exec_vars.at(temporal_epoch);
        // for all graphs
        for (const auto &graph_exec_var : graph_exec_vars) {
            const tt_graph_info& graph_info = this->parsed_netlist.graph_map.at(graph_exec_var.instrn.graph_name);
            // for all ops
            for (auto &[op_name, op_info]: graph_info.op_map) {
                // for all inputs
                const int num_input_bufs = n2p::get_op_num_input_bufs(op_info);
                for (int input_index = 0; input_index < num_input_bufs; input_index++) {
                    const auto& in_padding_info = op_info.input_padding.at(input_index);
                    if(in_padding_info.rt > 0 or in_padding_info.ct > 0 or in_padding_info.tm_padding) {
                        const DataFormat data_format = op_info.input_data_formats.at(input_index);
                        const TileDim tile_dim = op_info.input_tile_dims.at(input_index);
                        const float pad_value = in_padding_info.pad_value;
                        if(this->dram_pad_addr_table[data_format][pad_value] == 0) {
                            this->dram_pad_addr_table[data_format][pad_value] = start_addr + addr_offset;
                            addr_offset += n2p::get_format_tile_size_bytes(data_format, true, tile_dim) * MAX_PADDING_SCATTER_TILES;
                        }
                    }
                }
            }
        }
    }
    assert(addr_offset <= blob_size);
}

void Net2Pipe::emit_operand_and_pipe_info(const std::unordered_map<string, tt_op_info>& temporal_epoch_op_map, int epoch_id, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) const {
    std::unordered_map<std::string, std::map<int, std::unordered_map<tt_cxy_pair, std::set<router::unique_id_t>>>> consumer_to_pipe_id_map;
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<tt_cxy_pair, std::set<router::unique_id_t>>>> producer_to_pipe_id_map;
    for (auto &[pipe_unique_id, pipe] : epoch_context.pipes) {
        const string consumer_name = pipe.consumer_name();
        auto& consumer_map = consumer_to_pipe_id_map[consumer_name][pipe.consumer_input_index()];
        if(pipe.has_consumer()) {
        
            if(pipe.is_scatter()) {
                for(auto& buffer_ids: pipe.time_multiplexed_output_buffer_ids()) {
                    for(auto& buffer_id: buffer_ids) {
                        const auto& buffer = epoch_context.buffer_map.at(buffer_id);
                        consumer_map[buffer.core_location()].insert(pipe_unique_id);
                    }
                }
            }
            else {
                for(auto& buffer_id: pipe.output_buffer_ids()) {
                    auto& buffer = epoch_context.buffer_map.at(buffer_id);
                    consumer_map[buffer.core_location()].insert(pipe_unique_id);
                }
            }
        }
        else {
            //fmt::print("ERROR? No consumer for PIPE: {}\n", pipe_unique_id);
        }

        std::string input_name = "";
        if (name_is_op(consumer_name, epoch_context)) {
            const tt_op_info& op_info = temporal_epoch_op_map.at(consumer_name);
            input_name = op_info.input_names.at(pipe.consumer_input_index());
        }
        else if (name_is_queue(consumer_name)) {
            input_name = this->parsed_netlist.queue_map.at(consumer_name).input;
        }
        else {
            //fmt::print("ERROR? name not queue or op: {}\n", consumer_name);
        }

        for(auto& buffer_id: pipe.input_buffer_ids) {
            auto& buffer = epoch_context.buffer_map.at(buffer_id);
            producer_to_pipe_id_map[input_name][consumer_name][buffer.core_location()].insert(pipe_unique_id);
        }
    }
#if 0
    for(auto& [consumer_name, operand_map]: consumer_to_pipe_id_map) {
        if(name_is_queue(consumer_name)) {
            continue;
        }
        const tt_op_info& op_info = temporal_epoch_op_map.at(consumer_name);
        std::cout << "op_name: " << consumer_name << "\n";
        for(auto& [operand_id, output_location]: operand_map) {
            std::string input_name = op_info.input_names.at(operand_id);
            fmt::print("\toperand {}: \"{}\"\n", operand_id, input_name);
            for(auto& [location, pipes]: output_location) {
                fmt::print("\t\tpipes to: ({}, {}, {}): ", location.chip, location.y, location.x);
                for(auto& pipe_id: pipes) {
                    std::cout << pipe_id << ", ";
                }
                std::cout  << "\n";
            }
        } 
    }

    for(auto& [queue_name, queue_info]: this->parsed_netlist.queue_map) {
        if (netlist_parser::is_queue_fed_by_op(queue_info)
            and temporal_epoch_op_map.find(queue_info.input) != temporal_epoch_op_map.end()
        ) {
            // queue name fed by op in this temporal epoch
        }
    }

    for(auto& [op_name, op_info]: temporal_epoch_op_map) {
        std::cout << "op_name: " << op_name << "\n";
        for(auto& [operand_id, output_location]: consumer_to_pipe_id_map.at(op_name)) {
            std::string input_name = op_info.input_names.at(operand_id);
            fmt::print("\toperand {}: {} ({})\n", operand_id, input_name, name_is_queue(input_name) ? "queue" : name_is_op(input_name) ? "op" : "error");
            for(auto& [location, pipes]: output_location) {
                fmt::print("\t\tpipes to: ({}, {}, {}): ", location.chip, location.y, location.x);
                for(auto& pipe_id: pipes) {
                    std::cout << pipe_id << ", ";
                }
                std::cout  << "\n";
            }
        }
        for(auto& [output_name, pipe_input_locations]: producer_to_pipe_id_map.at(op_name)) {
            fmt::print("\toutput: {} ({})\n", output_name, name_is_queue(output_name) ? "queue" : name_is_op(output_name) ? "op" : "error");
            for(auto& [location, pipes]: pipe_input_locations) {
                fmt::print("\t\tpipes from: ({}, {}, {}): ", location.chip, location.y, location.x);
                for(auto& pipe_id: pipes) {
                    std::cout << pipe_id << ", ";
                }
                std::cout  << "\n";
            }
        } 
    }
#endif 

    YAML::Emitter out_yaml;
    out_yaml << YAML::BeginMap;
#if 0    
    out_yaml << YAML::Key << "queues";
    out_yaml << YAML::Value;
    out_yaml << YAML::BeginMap;
    // TODO: Output queues
    out_yaml << YAML::EndMap;
#endif
    out_yaml << YAML::Key << "ops";
    out_yaml << YAML::Value;
    out_yaml << YAML::BeginMap; // op map
    for(auto& [op_name, op_info]: temporal_epoch_op_map) {
        out_yaml << YAML::Key << op_name;
        out_yaml << YAML::Value;
        out_yaml << YAML::BeginMap; // op detail     
        out_yaml << YAML::Key << "inputs";
        out_yaml << YAML::Value;
        out_yaml << YAML::BeginSeq; // operands
        if(consumer_to_pipe_id_map.find(op_name) != consumer_to_pipe_id_map.end()) {
            int operand_count = 0;
            for(auto& [operand_id, output_location]: consumer_to_pipe_id_map.at(op_name)) {
                std::string input_name = op_info.input_names.at(operand_id);
                if (operand_count < operand_id) {
                    log_warning(tt::LogNet2Pipe, "Incomplete operand map for OP : {} ", op_name);
                }
                operand_count++;
                //fmt::print("\toperand {}: {} ({})\n", operand_id, input_name, name_is_queue(input_name) ? "queue" : name_is_op(input_name) ? "op" : "error");
                std::string type = name_is_queue(input_name) ? "queue" : name_is_op(input_name, epoch_context) ? "op" : "error";

                out_yaml << YAML::BeginMap; // operand detail
                out_yaml << YAML::Key << "name";
                out_yaml << YAML::Value << input_name;
                out_yaml << YAML::Key << "type";
                out_yaml << YAML::Value << type;
                out_yaml << YAML::Key << "pipes";
                out_yaml << YAML::Value;
                out_yaml << YAML::BeginMap; // pipe locations
                for(auto& [location, pipes]: output_location) {
                    std::string loc_str = fmt::format("{}-{}-{}", location.chip, location.y, location.x);
                    out_yaml << YAML::Key << loc_str;
                    out_yaml << YAML::Value;
                    out_yaml << YAML::BeginSeq; // pipe ids
                    for(auto& pipe_id: pipes) {
                        out_yaml << deterministic_id_map.get_deterministic_key(pipe_id);
                    }
                    out_yaml << YAML::EndSeq; // pipe ids
                }
                out_yaml << YAML::EndMap; // pipe locations
                out_yaml << YAML::EndMap;  // operand detail
            }
        }
        out_yaml << YAML::EndSeq; // operands
        
        out_yaml << YAML::Key << "outputs";
        out_yaml << YAML::Value;
        out_yaml << YAML::BeginSeq; // outputs
        if(producer_to_pipe_id_map.find(op_name) != producer_to_pipe_id_map.end()) {
            for(auto& [output_name, pipe_input_locations]: producer_to_pipe_id_map.at(op_name)) {
                //fmt::print("\toutput: {} ({})\n", output_name, name_is_queue(output_name) ? "queue" : name_is_op(output_name) ? "op" : "error");
                std::string type = name_is_queue(output_name) ? "queue" : name_is_op(output_name, epoch_context) ? "op" : "error";
                out_yaml << YAML::BeginMap; // output detail
                out_yaml << YAML::Key << "name";
                out_yaml << YAML::Value << output_name;
                out_yaml << YAML::Key << "type";
                out_yaml << YAML::Value << type;
                out_yaml << YAML::Key << "pipes";
                out_yaml << YAML::Value;
                out_yaml << YAML::BeginMap; // pipe locations

                for(auto& [location, pipes]: pipe_input_locations) {
                    //fmt::print("\t\tpipes from: ({}, {}, {}): ", location.chip, location.y, location.x);
                    std::string loc_str = fmt::format("{}-{}-{}", location.chip, location.y, location.x);
                    out_yaml << YAML::Key << loc_str;
                    out_yaml << YAML::Value;
                    out_yaml << YAML::BeginSeq; // pipe ids
                    for(auto& pipe_id: pipes) {
                        out_yaml << deterministic_id_map.get_deterministic_key(pipe_id);
                    }
                    out_yaml << YAML::EndSeq; // pipe ids
                }
                out_yaml << YAML::EndMap; // pipe locations
                out_yaml << YAML::EndMap;  // output detail
            }
        }
        out_yaml << YAML::EndSeq; // outputs
        out_yaml << YAML::EndMap; // op detail
    }
    out_yaml << YAML::EndMap; // op map

    const std::string cmd = "mkdir -p " + this->output_dir + "/reports";
    const int status = system(cmd.c_str());
    //n2p::Log() << "return value=" << status << std::endl;

    std::ofstream out_file;
    const std::string yaml_out_file_path = this->output_dir + "/reports/op_to_pipe_map_temporal_epoch_" + std::to_string(epoch_id) + ".yaml";
    out_file.open(yaml_out_file_path);
    out_file << out_yaml.c_str();
    out_file.close();
}

uint32_t Net2Pipe::get_dram_pad_addr(DataFormat df, float pad_val) const {
    return this->dram_pad_addr_table.at(df).at(pad_val);
}

void Net2Pipe::emit_padding_buffers(YAML::Emitter &out, const temporal_epoch_context& epoch_context, const n2p::DeterministicKeyMap& deterministic_id_map) const {
    for(const auto& [key, buf]: epoch_context.pad_buffers_db) {

        out << YAML::BeginMap;
        out << YAML::Key << ("buffer_" + std::to_string(deterministic_id_map.get_deterministic_key(buf.uniqid)));
        out << YAML::Comment(buf.comment);
        out << YAML::Value << YAML::BeginMap;

        out << SET_KEY_VAL("md_op_name", buf.md_op_name);
        out << SET_KEY_VAL("buffer_type", n2p::c_Unknown);
        out << SET_KEY_VAL("id", buf.id);
        out << SET_KEY_VAL("uniqid", deterministic_id_map.get_deterministic_key(buf.uniqid));
        out << SET_KEY_VAL("epoch_tiles", buf.epoch_tiles);
        out << SET_KEY_VAL("chip_id", YAML::Flow << YAML::BeginSeq << buf.chip_id << YAML::EndSeq);
        out << SET_KEY_VAL("core_coordinates", SET_COORD2(buf.core_r, buf.core_c));
        out << SET_KEY_VAL("size_tiles", buf.size_tiles);
        out << SET_KEY_VAL("scatter_gather_num_tiles", buf.scatter_gather_num_tiles);
        out << SET_KEY_VAL("tiles_per_input", buf.tiles_per_input);
        out << SET_KEY_VAL("is_scatter", 1);
        out << SET_KEY_VAL("replicate", buf.replicate); // dm_note: must be the same as the non-padding dram io inputs, scatter buffers other than the first one must remain unconnected
        out << SET_KEY_VAL("is_padding", 1);
        out << SET_KEY_VAL("tile_size", buf.tile_size);
        //out << SET_KEY_VAL("tile_clear_granularity", buf.tile_clear_granularity);
        out << SET_KEY_VAL("dram_io_flag_is_remote", 0);
        out << SET_KEY_VAL("dram_buf_flag", buf.dram_buf_flag);
        out << SET_KEY_VAL("prefetch_type", buf.prefetch_type);
        out << SET_KEY_VAL("dram_buf_streaming", 0);
        out << SET_KEY_VAL("write_dram_buf_flag", 0);
        out << SET_KEY_VAL("dram_prefetch_incoming_noc_id", 0);
        out << SET_KEY_VAL("dram_chan", buf.dram_chan);
        out << SET_KEY_VAL("dram_sub_chan", this->get_queue_dram_subchannel(buf.uniqid, epoch_context));
        out << SET_KEY_VAL("dram_addr", uint_to_string(buf.dram_addr));
        out << SET_KEY_VAL("q_slots", buf.q_slots);
        out << SET_KEY_VAL("dram_io_flag", buf.dram_io_flag);
        out << SET_KEY_VAL("dram_io_allow_overwrite", 0);
        out << SET_KEY_VAL("dram_io_skip_flow_ctrl", 0);
        out << SET_KEY_VAL("dram_ram_flag", 0);
        out << SET_KEY_VAL("use_shadow_rd_ptr", 0);
        out << SET_KEY_VAL("ublock_rt", 0);
        out << SET_KEY_VAL("ublock_ct", 0);
        out << SET_KEY_VAL("mblock_m", 0);
        out << SET_KEY_VAL("mblock_n", 0);
        out << SET_KEY_VAL("mblock_k", 0);
        out << SET_KEY_VAL("untilized_output", 0);
        out << SET_KEY_VAL("untilized_output_full_r_dim", 0);
        out << SET_KEY_VAL("untilized_output_full_c_dim", 0);
        out << SET_KEY_VAL("untilized_output_r_dim", 0);
        out << SET_KEY_VAL("untilized_output_c_dim", 0);
        out << SET_KEY_VAL("untilized_output_z_dim", 0);
        out << SET_KEY_VAL("untilized_output_type_0_zdim", 0);
        out << SET_KEY_VAL("untilized_output_type_1_zdim", 0);

        out << YAML::EndMap;
        out << YAML::EndMap;
    }
}

