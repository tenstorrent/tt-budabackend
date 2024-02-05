// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_parser.hpp"

#include "common/tt_parallel_for.h"
#include "device/cpuset_lib.hpp"
#include "hlks/inc/hlk_api.h"
#include "netlist_basic_info_types.hpp"
#include "netlist_op_info_types.hpp"
#include "netlist_utils.hpp"
#include "size_lib.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/logger.hpp"
#include "utils/scoped_timer.hpp"

using namespace netlist_utils;

void netlist_parser::clear() {
    queue_map.clear();
    graph_map.clear();
    program_order.clear();
    program_map.clear();
    op_graph_map.clear();
    producer_consumer_map.clear();
    initialized = false;
}

void netlist_parser::parse_devices(const YAML::Node& devices, tt_device_info &device_info) {
    for (YAML::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        if (it->first.as<std::string>() == "arch") {
            if (it->second.IsSequence()) {
                // e.g. [grayskull, wormhole] specifies that this netlist works for either arch
                // In this case we select the arch through env variable ARCH_NAME
                std::vector<tt::ARCH> viable_archs;
                for (const YAML::Node &node : it->second) {
                    viable_archs.push_back(get_arch_from_string(node.as<string>()));
                }
                const char *env_arch = std::getenv("ARCH_NAME");
                if (env_arch == nullptr) {
                    log_fatal("Must specify ARCH_NAME in cmdline if netlist supports multiple arch");
                }
                tt::ARCH device_arch = get_arch_from_string(std::string(env_arch));
                if (std::find(viable_archs.begin(), viable_archs.end(), device_arch) == viable_archs.end()) {
                    log_fatal(
                        "Command line ARCH_NAME specifies an arch that is not listed in the netlist. Check "
                        "device_info: arch");
                }
                device_info.arch = device_arch;
            } else {
                device_info.arch = get_arch_from_string(it->second.as<string>());
            }
        } else {
            log_fatal(
                "Field {} is not supported for tt_device_info settings", it->first.as<std::string>());
        }
    }
}

void netlist_parser::parse_queues(const YAML::Node& queues, unordered_map<string, tt_queue_info> &queue_map) {
    string name;

    for (YAML::const_iterator it = queues.begin(); it != queues.end(); ++it) {
        name = it->first.as<std::string>();
        int pre_insert_size = queue_map.size();
        queue_map[name].name = name;
        queue_map[name].dim.ublock_order = Dim::R;  // Always default to row order.
        for (YAML::const_iterator iit = it->second.begin(); iit != it->second.end(); ++iit) {
            if (iit->first.as<std::string>() == "input") {
                queue_map[name].input = iit->second.as<string>();
                if (is_queue_fed_by_op(queue_map[name])) {
                    producer_consumer_map[queue_map[name].input].insert(name);
                }
            } else if (iit->first.as<std::string>() == "input_tms") {
                parse_queue_tm_ops(iit->second, iit->first.as<std::string>(), queue_map[name]);
            } else if (iit->first.as<std::string>() == "entries") {
                queue_map[name].entries = iit->second.as<int>();
            } else if (iit->first.as<std::string>() == "grid_size") {
                log_assert(iit->second.size() == 2, "grid_size must contain 2 values");
                // grid size {r, c}
                queue_map[name].grid_size.r = iit->second[0].as<int>();
                queue_map[name].grid_size.c = iit->second[1].as<int>();
            } else if (iit->first.as<std::string>() == "t") {
                queue_map[name].dim.t = iit->second.as<int>();
            } else if (iit->first.as<std::string>() == "ublock") {
                log_assert(iit->second.size() == 2, "ublock must contain 2 values");
                // grid size {r, c}
                queue_map[name].dim.ublock_rt = iit->second[0].as<int>();
                queue_map[name].dim.ublock_ct = iit->second[1].as<int>();
            } else if (iit->first.as<std::string>() == "mblock") {
                log_assert(iit->second.size() == 2, "mblock must contain 2 values");
                // grid size {r, c}
                queue_map[name].dim.mblock_m = iit->second[0].as<int>();
                queue_map[name].dim.mblock_n = iit->second[1].as<int>();
            } else if (iit->first.as<std::string>() == "ublock_order") {
                queue_map[name].dim.ublock_order = netlist_utils::get_dim_enum(iit->second.as<string>());
            } else if (iit->first.as<std::string>() == "target_device") {
                if (iit->second.IsSequence()) {
                    for (const YAML::Node &node : iit->second) {
                        queue_to_devices_map[name].push_back(node.as<int>());
                    }
                } else {
                    queue_map[name].target_device = iit->second.as<int>();
                }
            } else if (iit->first.as<std::string>() == "loc") {
                queue_map[name].loc = get_queue_location_enum(iit->second.as<string>());
                if (queue_map[name].loc == QUEUE_LOCATION::DRAM) {
                    log_assert(
                        it->second["dram"].size(), "dram allocation information needs to be supplied if location is dram");
                    for (const YAML::Node &per_core_alloc_info : it->second["dram"]) {
                        queue_map[name].alloc_info.push_back(tt_queue_allocation_info{
                            .channel = per_core_alloc_info[0].as<std::uint32_t>(),
                            .address = per_core_alloc_info[1].as<std::uint32_t>()});
                    }
                    if (it->second["read_ports"]) {
                        if (this->device_info.arch != tt::ARCH::WORMHOLE_B0) {
                            log_fatal("read_ports setting for DRAM buffers is only available for wormhole_b0");
                        }            
                        log_assert(it->second["dram"].size() == it->second["read_ports"].size(), "queue {}: read_ports setting must have one element per queue buffer", name); 
                        int i = 0;
                        for (const YAML::Node &read_port_info : it->second["read_ports"]) {
                            int32_t read_port = read_port_info.as<std::int32_t>();
                            log_assert(read_port >= 0 && read_port <= 2, "queue {}: read_ports setting must be in the range 0-2", name); 
                            queue_map[name].alloc_info[i].read_port = read_port;                            
                            i++;
                        }            
                    }
                    if (it->second["write_ports"]) {
                        if (this->device_info.arch != tt::ARCH::WORMHOLE_B0) {
                            log_fatal( "write_ports setting for DRAM buffers is only available for wormhole_b0");
                        }            
                        log_assert(it->second["dram"].size() == it->second["write_ports"].size(), "queue {}: write_ports setting must have one element per queue buffer", name); 
                        int i = 0;
                        for (const YAML::Node &write_port_info : it->second["write_ports"]) {
                            int32_t write_port = write_port_info.as<std::int32_t>();
                            log_assert(write_port >= 0 && write_port <= 2, "queue {}: write_ports setting must be in the range 0-2", name); 
                            queue_map[name].alloc_info[i].write_port = write_port;
                            i++;
                        }            
                    }
                } else if (queue_map[name].loc == QUEUE_LOCATION::HOST) {
                    log_assert(
                        it->second["host"].size(), "host allocation information needs to be supplied if location is host");
                    if (it->second["host"]) {
                        for (const YAML::Node &host_alloc_info : it->second["host"]) {
                            // Host queues now have channel. Allow one to be omitted for back-compat, temporarily.
                            if (host_alloc_info.size() != 2) {
                                queue_map[name].alloc_info.push_back(tt_queue_allocation_info{
                                    .channel = 0x0,
                                    .address = host_alloc_info.as<std::uint32_t>()});
                            } else {
                                queue_map[name].alloc_info.push_back(tt_queue_allocation_info{
                                    .channel = host_alloc_info[0].as<std::uint32_t>(),
                                    .address = host_alloc_info[1].as<std::uint32_t>()});
                            }
                        }
                    }
                }
            } else if (iit->first.as<std::string>() == "type") {
                queue_map[name].type = get_io_type_enum(iit->second.as<string>());
            } else if (iit->first.as<std::string>() == "layout") {
                queue_map[name].layout = get_queue_layout_enum(iit->second.as<string>());
            } else if (iit->first.as<std::string>() == "alias") {
                queue_map[name].alias = iit->second.as<string>();
            } else if (iit->first.as<std::string>() == "dram") {
                // Should be parsed already
            } else if (iit->first.as<std::string>() == "read_ports") {
                // Should be parsed already
            } else if (iit->first.as<std::string>() == "write_ports") {
                // Should be parsed already
            } else if (iit->first.as<std::string>() == "host") {
                // Should be parsed already
            } else if (iit->first.as<std::string>() == "df") {
                queue_map[name].data_format = get_data_format(iit->second.as<string>());
            } else if (iit->first.as<std::string>() == "tile_dim") {
                queue_map[name].tile_dim = get_tile_dim_from_array({iit->second[0].as<int>(), iit->second[1].as<int>()});
                log_assert(queue_map[name].tile_dim != TileDim::Invalid, "Invalid tile dim for queue {}", name);
                log_assert(this->device_info.arch == tt::ARCH::GRAYSKULL ? queue_map[name].tile_dim == TileDim::Dim32x32 : true, "Grayskull only supports 32x32 (default) tile sizes");               
            } else {
                log_fatal(
                    "Field {} is not supported for tt_queue_info settings",
                    iit->first.as<std::string>());
            }
        }
        log_assert(
            queue_map.size() > (uint32_t)pre_insert_size,
            "Error: failed to insert queue into queue map, probably due to duplicate name, for queue name {}",
            name);
    }
}

static void parse_op_attribute_ingress_channels(tt_op_info &op_info, const YAML::Node &attribute_node) {
    op_info.attributes.ethernet_datacopy_attr.ingress_channels = attribute_node.as<std::vector<int>>();
    if (op_info.attributes.ethernet_datacopy_attr.ingress_channels.size() == 0) {
        log_fatal("ingress_channels must be a non-empty list of integers");
    }
}
static void parse_op_attribute_egress_channels(tt_op_info &op_info, const YAML::Node &attribute_node) {
    op_info.attributes.ethernet_datacopy_attr.egress_channels = attribute_node.as<std::vector<int>>();
    if (op_info.attributes.ethernet_datacopy_attr.egress_channels.size() == 0) {
        log_fatal("egress_channels must be a non-empty list of integers");
    }
}
static void parse_op_attribute_dest_device(tt_op_info &op_info, const YAML::Node &attribute_node) {
    op_info.attributes.ethernet_datacopy_attr.dest_device = attribute_node.as<int>();
    if (op_info.attributes.ethernet_datacopy_attr.dest_device < 0) {
        log_fatal("dest_device must be a non-negative integer");
    }
}

void netlist_parser::validation::validate_splice_op_attributes(tt_op_info const &op_info, int input_index) {
    tt_splice_info const &splice_input_attributes = op_info.attributes.splice_infos.at(input_index);
    bool invalid_length = splice_input_attributes.length <= 0;
    if (invalid_length) {
        log_fatal(
            "Invalid length {} for input {} for splice op {}. Must be > 0",
            op_info.attributes.splice_infos.at(input_index).length,
            input_index,
            op_info.name);
    }

    bool invalid_stride = splice_input_attributes.stride <= 0;
    if (invalid_stride) {
        log_fatal(
            "Invalid stride {} for input {} for splice op {}. Must be > 0",
            op_info.attributes.splice_infos.at(input_index).stride,
            input_index,
            op_info.name);
    }

    bool invalid_start_index = splice_input_attributes.index < 0;
    if (invalid_start_index) {
        log_fatal(
            "Invalid start index {} for input {} of splice op {}. Must be >= 0 and <= # of input T or ublock",
            op_info.attributes.splice_infos.at(input_index).index,
            input_index,
            op_info.name);
    }
}

void netlist_parser::parse_op(const YAML::Node& op, tt_op_info &op_info) {
    // Parse basic mandatory settings
    for (const auto &op_field : op) {
        if (op_field.first.as<std::string>() == "type") {
            op_info.type = op_field.second.as<string>();
            op_info.attributes.op_type = op_info.type;
        } else if (op_field.first.as<std::string>() == "t") {
            op_info.output_dim.t = op_field.second.as<int>();
        } else if (op_field.first.as<std::string>() == "ublock") {
            log_assert(op_field.second.size() == 2, "ublock must contain 2 values");
            // grid size {r, c}
            op_info.output_dim.ublock_rt = op_field.second[0].as<int>();
            op_info.output_dim.ublock_ct = op_field.second[1].as<int>();
        } else if (op_field.first.as<std::string>() == "mblock") {
            log_assert(op_field.second.size() == 2, "mblock must contain 2 values");
            // grid size {r, c}
            op_info.output_dim.mblock_m = op_field.second[0].as<int>();
            op_info.output_dim.mblock_n = op_field.second[1].as<int>();
            if ((op_info.type == "buffer") && (op_info.output_dim.mblock_m > 1)) {
                log_fatal("Output mblock_m must be set to 1 per core for buffer op {} ", op_info.name);
            }
        } else if (op_field.first.as<std::string>() == "tile_dim") {
            op_info.output_tile_dim = get_tile_dim_from_array({op_field.second[0].as<int>(), op_field.second[1].as<int>()});
            log_assert(op_info.output_tile_dim != TileDim::Invalid, "Invalid tile dim for op {}", op_info.name);
            log_assert(this->device_info.arch == tt::ARCH::GRAYSKULL ? op_info.output_tile_dim == TileDim::Dim32x32 : true, "Grayskull only supports 32x32 (default) tile sizes");
        } else if (op_field.first.as<std::string>() == "attributes") {
            continue;
        } else if (op_field.first.as<std::string>() == "out_df") {
            op_info.output_data_format = get_data_format(op_field.second.as<string>());
        } else if (op_field.first.as<std::string>() == "acc_df") {
            op_info.dest_accumulate_data_format = get_data_format(op_field.second.as<string>());
        } else if (op_field.first.as<std::string>() == "intermed_df") {
            op_info.intermed_data_format = get_data_format(op_field.second.as<string>());
        } else if (op_field.first.as<std::string>() == "math_fidelity") {
            op_info.math_fidelity = get_fidelity(op_field.second.as<string>());
        } else if (op_field.first.as<std::string>() == "untilize_output") {
            op_info.untilize_output = op_field.second.as<bool>();
        } else if (op_field.first.as<std::string>() == "grid_transpose") {
            op_info.grid_transpose = op_field.second.as<bool>();
        } else if (op_field.first.as<std::string>() == "gradient_op") {
            op_info.gradient_op = op_field.second.as<bool>();
        } else if (op_field.first.as<std::string>() == "grid_loc") {
            op_info.grid_loc = op_field.second.as<std::vector<int>>();
        } else if (op_field.first.as<std::string>() == "grid_size") {
            const auto &grid_size_vector = op_field.second.as<std::vector<int>>();
            log_assert(grid_size_vector.size() == 2, "Grid size should be 2 for this op");
            op_info.grid_size = {
                .r = static_cast<uint32_t>(grid_size_vector.at(0)), .c = static_cast<uint32_t>(grid_size_vector.at(1))};
        } else if (op_field.first.as<std::string>() == "buf_size_mb") {
            op_info.buf_size_mb = op_field.second.as<int>();
        } else if (op_field.first.as<std::string>() == "overlay_size") {
            op_info.overlay_size = op_field.second.as<int>();
        } else if (op_field.first.as<std::string>() == "input_buf_min_size_tiles") {
            op_info.input_buf_min_size_tiles = op_field.second.as<std::vector<int>>();
        } else if (op_field.first.as<std::string>() == "input_dram_io_buf_size_tiles") {
            op_info.input_dram_io_buf_size_tiles = op_field.second.as<std::vector<int>>();
        } else if (op_field.first.as<std::string>() == "ublock_order") {
            op_info.output_dim.ublock_order = netlist_utils::get_dim_enum(op_field.second.as<string>());
        } else if (op_field.first.as<std::string>() == "inputs") {
            op_info.input_names = op_field.second.as<std::vector<std::string>>();
        } else if (op_field.first.as<std::string>() == "forked_dram_inputs") {
            log_assert(
                op_field.second.IsSequence(),
                " Forked dram inputs must be a vector of queue_name:op_name map");
            for (const auto &forked_dram_input_it : op_field.second) {
                for (const auto &forked_dram_input_map_it : forked_dram_input_it)
                    op_info.forked_dram_input_names.insert(
                        {forked_dram_input_map_it.first.as<string>(), forked_dram_input_map_it.second.as<string>()});
            }
        } else if (op_field.first.as<std::string>() == "in_df") {
            for (const YAML::Node &in_df : op_field.second) {
                op_info.input_data_formats.push_back(get_data_format(in_df.as<string>()));
            }
        } else if (
            (op_field.first.as<std::string>().find("tms") != std::string::npos) and
            (op_field.first.as<std::string>().find("input") != std::string::npos)) {
            continue;
        }
        else if (
            (op_field.first.as<std::string>().find("unpad") != std::string::npos) and
            (op_field.first.as<std::string>().find("input") != std::string::npos)) {
            continue;
        }
        else if (
            (op_field.first.as<std::string>().find("pad") != std::string::npos) and
            (op_field.first.as<std::string>().find("input") != std::string::npos)) {
            continue;
        } else {
            log_fatal(
                "Field {} is not supported for tt_op_info settings", op_field.first.as<std::string>());
        }

    }

    // By default, front-end doesn't require a minimal input buffer size, so we default to 0 and let net2pipe decide.
    // Similar for dram io input buf size - we default to 0 and let pipegen decide. 
    if (op_info.input_buf_min_size_tiles.size() == 0) {
        for (int i = 0; i < op_info.input_names.size(); i++) {
            op_info.input_buf_min_size_tiles.push_back(0);
        }
    }
    if (op_info.input_dram_io_buf_size_tiles.size() == 0) {
        for (int i = 0; i < op_info.input_names.size(); i++) {
            op_info.input_dram_io_buf_size_tiles.push_back(0);
        }
    }
    // Set default input padding to 0,0, ""
    const int num_operand_inputs = op_info.input_names.size();
    for(int i = 0; i < num_operand_inputs; i++) {
        op_info.input_padding.push_back({0 ,0, 0.0, false, false});
        op_info.input_unpadding.push_back({0, 0, 0.0, false, false});
    }

    // attributes/tms may vary on other op info fields, so do this second
    for (const auto &op_field : op) {
        if ((op_field.first.as<std::string>().find("tms") != std::string::npos) and
            (op_field.first.as<std::string>().find("input") != std::string::npos)) {
            parse_tm_ops(op_field.second, op_field.first.as<std::string>(), op_info);
        }
        else if((op_field.first.as<std::string>().find("unpad") != std::string::npos) and
            (op_field.first.as<std::string>().find("input") != std::string::npos)) {
            parse_unpadding(op_field.second, op_field.first.as<std::string>(), op_info);
        }
        else if((op_field.first.as<std::string>().find("pad") != std::string::npos) and
            (op_field.first.as<std::string>().find("input") != std::string::npos)) {
            parse_padding(op_field.second, op_field.first.as<std::string>(), op_info);
        } else if (op_field.first.as<std::string>() == "attributes") {
            log_assert(op_field.second.size() >= 1, "attributes must contain at least 1 value");
            for (YAML::const_iterator iiit = op_field.second.begin(); iiit != op_field.second.end(); ++iiit) {
                if (iiit->first.as<string>() == "kernel_broadcast" || iiit->first.as<string>() == "kernel_broadcast_per_t") {
                    bool per_t = (iiit->first.as<string>() == "kernel_broadcast_per_t");
                    if (op_info.attributes.kernel_broadcast.size() != op_info.input_names.size()) {
                        op_info.attributes.kernel_broadcast = std::vector<std::pair<int, bool>>(op_info.input_names.size(), {0, false});
                    }
                    for (YAML::const_iterator kb_it = iiit->second.begin(); kb_it != iiit->second.end(); ++kb_it) {
                      std::string input_index_str = kb_it->first.as<std::string>();
                      auto num_pos = input_index_str.find_first_of('_');
                      if (!(num_pos != std::string::npos)) {
                        log_fatal(
                                  "Invalid kernel broadcast syntax {}! Found on op {}, type {}.",
                                  input_index_str,
                                  op_info.name,
                                  op_info.type);
                      }
                      int input_index = stoi(input_index_str.substr(num_pos + 1));
                      int val = kb_it->second.as<int>();
                      log_assert(
                          op_info.attributes.kernel_broadcast.at(input_index).first == 0,
                          "Duplicate kernel broadcast attribute for input {} on op {}!",
                          input_index,
                          op_info.name);
                      op_info.attributes.kernel_broadcast.at(input_index) = {val, per_t};
                    }
                } else if (iiit->first.as<string>() == "single_tile") {
                   op_info.attributes.kernel_broadcast = std::vector<std::pair<int, bool>>(op_info.input_names.size(), {0, false});
                   for (const auto &input_index : iiit->second.as<std::vector<int>>()) {
                        log_assert(
                            input_index < op_info.input_names.size(),
                            "Op={} has kernel_broadcast attribute which must refer to input index within range of inputs "
                            "[0:{}]",
                            op_info.name,
                            op_info.input_names.size() - 1);

                        op_info.attributes.kernel_broadcast.at(input_index) = {1, false}; 
                    }
                } else if (iiit->first.as<string>() == "m_k") {
                    op_info.attributes.m_k = iiit->second.as<int>();
                } else if (iiit->first.as<string>() == "u_kt") {
                    op_info.attributes.u_kt = iiit->second.as<int>();
                } else if (iiit->first.as<string>() == "bias") {
                    op_info.attributes.bias = iiit->second.as<bool>();
                } else if (iiit->first.as<string>() == "sfpu_op") {
                    op_info.attributes.sfpu_op = netlist_utils::get_sfpu_op(iiit->second.as<string>());
                } else if (iiit->first.as<string>() == "accumulate") {
                    op_info.attributes.accumulate = iiit->second.as<bool>();
                } else if (iiit->first.as<string>() == "vector") {
                    op_info.attributes.vector_mode = netlist_utils::get_dim_enum(iiit->second.as<string>());
                } else if (iiit->first.as<string>() == "approximate_mode") {
                    op_info.attributes.approximate_mode = iiit->second.as<bool>();
                } else if ((iiit->first.as<string>() == "relu_en")) {
                    op_info.attributes.relu_en = iiit->second.as<bool>();
                } else if ((iiit->first.as<string>() == "relu_threshold")) {
                    op_info.attributes.relu_threshold = iiit->second.as<float>();
                    if (op_info.attributes.relu_threshold < 0.0f) {
                        log_fatal("Relu threshold for {} op must be >= 0.0", op_info.type);
                    }
                } else if ((iiit->first.as<string>() == "relu_mode")) {
                    op_info.attributes.relu_mode = netlist_utils::get_relu_mode_enum(iiit->second.as<string>());
                    if (op_info.attributes.relu_en){
                        log_assert((op_info.attributes.relu_mode == ReluMode::Max || op_info.attributes.relu_mode == ReluMode::Min),
                        "Relu mode must be min or max when relu en flag is set to true");
                    }
                } else if ((iiit->first.as<string>() == "srnd_fpu_en")) {
                    //Only here for backwards compatibility until safe to remove
                    op_info.attributes.stoch_rnd_mode = (iiit->second.as<bool>()) ? StochRndMode::Fpu : StochRndMode::None;
                    if ((op_info.attributes.stoch_rnd_mode != StochRndMode::None) && this->device_info.arch != tt::ARCH::WORMHOLE_B0) {
                        log_fatal("Stochastic rounding is only available for wormhole_b0");
                    }
                } else if ((iiit->first.as<string>() == "stoch_rnd_mode")) {
                    op_info.attributes.stoch_rnd_mode = netlist_utils::get_stoch_rnd_mode(iiit->second.as<string>());
                    if ((op_info.attributes.stoch_rnd_mode != StochRndMode::None) && (this->device_info.arch != tt::ARCH::WORMHOLE_B0)) {
                        log_fatal("Stochastic rounding is only available for wormhole_b0");
                    }
                } else if (
                    (iiit->first.as<string>() == "identity") &&
                    ((op_info.type == "matmul") or (op_info.type == "fused_op"))) {
                    op_info.attributes.identity = iiit->second.as<bool>();
                } else if (
                    (iiit->first.as<string>() == "num_index_tiles") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op") or (op_info.type == "buffer")))) {
                    op_info.attributes.num_index_tiles = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "num_sparse_tiles") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op")))) {
                    op_info.attributes.num_sparse_tiles = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "num_columns") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op")))) {
                    op_info.attributes.num_columns = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "sparse_tile_ptr_bits") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op") or (op_info.type == "buffer")))) {
                    op_info.attributes.sparse_tile_ptr_bits = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "sparse_ublock_idx_bits") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op") or (op_info.type == "buffer")))) {
                    op_info.attributes.sparse_ublock_idx_bits = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "indices_len") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op")))) {
                    op_info.attributes.indices_len = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "fracture_factor") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op")))) {
                    op_info.attributes.fracture_factor = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "starting_row") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op")))) {
                    op_info.attributes.starting_row = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "act_t") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op")))) {
                    op_info.attributes.act_t = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "num_nz_strips") &&
                    ((op_info.type == "matmul" or (op_info.type == "fused_op") or (op_info.type == "buffer")))) {
                    if (iiit->second.size() != op_info.grid_size_x() * op_info.grid_size_y()) {
                        log_fatal(
                            "num_nz_strips attribute for {} op must be set for all cores!",
                            op_info.name);
                    }
                    op_info.attributes.num_nz_strips = iiit->second.as<std::vector<int>>();
                } else if (
                    (iiit->first.as<string>() == "min_buffer_input") &&
                    ((op_info.type == "matmul") or (op_info.type == "fused_op") or (op_info.type == "buffer") or (op_info.type == "depthwise"))) {
                    int input_index = iiit->second.as<int>();
                    if (input_index > 1) {
                        log_fatal("min_buffer_input attribute for {} op must be 0 or 1", op_info.name);
                    } else if ((input_index == 1) && (op_info.type == "buffer")) {
                        log_fatal("min_buffer_input attribute for {} op must be 0", op_info.name);
                    } else if ((input_index == 1) && (op_info.type == "depthwise")) {
                        log_fatal("min_buffer_input attribute for {} op must be 0", op_info.name);
                    }
                    op_info.attributes.min_input_buffer[0] = input_index == 0;
                    op_info.attributes.min_input_buffer[1] = input_index == 1;
                } else if (
                    (iiit->first.as<string>() == "l1_acc") &&
                    ((op_info.type == "matmul") || netlist_utils::is_valid_depthwise_op(op_info.type))) {
                    op_info.attributes.l1_acc = iiit->second.as<bool>();
                } else if (
                    (iiit->first.as<string>() == "dim") &&
                    ((op_info.type == "reduce" or (op_info.type == "fused_op")))) {
                    op_info.attributes.reduce_dim = netlist_utils::get_dim_enum(iiit->second.as<string>());
                } else if (
                    (iiit->first.as<string>() == "type") &&
                    ((op_info.type == "reduce" or (op_info.type == "fused_op")))) {
                    op_info.attributes.reduce_type = netlist_utils::get_valid_reduce_func(iiit->second.as<string>());
                } else if (
                    (iiit->first.as<string>() == "z") &&
                    ((op_info.type == "reduce" or (op_info.type == "fused_op") or (op_info.type == "matmul")))) {
                    op_info.attributes.z = iiit->second.as<int>();
                    if ((op_info.type == "reduce") && (op_info.attributes.reduce_dim != Dim::Z)) {
                        log_fatal(
                            "z attribute for {} op is only valid for reduce max dim: z", op_info.type);
                    } else if ((op_info.type == "matmul") && (!op_info.attributes.accumulate)) {
                        log_fatal(
                            "z attribute for {} op is only valid for matmul accumulate", op_info.type);
                    }
                } else if (
                    (iiit->first.as<string>() == "p") &&
                    ((op_info.type == "dropout" or (op_info.type == "fused_op")))) {
                    op_info.attributes.probability = iiit->second.as<float>();
                    if ((op_info.attributes.probability < 0.0f) || (op_info.attributes.probability > 1.0f)) {
                        log_fatal(
                            "Probability attribute for {} op must be in [0.0:1.0] range", op_info.type);
                    }
                    op_info.attributes.scale =
                        op_info.attributes.probability == 1.0 ? 1.0 : 1.0 / (1.0 - op_info.attributes.probability);
                } else if (
                    (iiit->first.as<string>() == "seed") &&
                    ((op_info.type == "dropout" or (op_info.type == "fused_op")))) {
                    op_info.attributes.seed = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "slope") &&
                    ((op_info.type == "lrelu" or (op_info.type == "fused_op")))) {
                    op_info.attributes.slope = iiit->second.as<float>();
                } else if (
                    (iiit->first.as<string>() == "exp") &&
                    ((op_info.type == "power" or (op_info.type == "fused_op")))) {
                    op_info.attributes.exponent = iiit->second.as<int>();
                } else if ((iiit->first.as<string>() == "fused_op_id") && (op_info.type == "fused_op")) {
                    op_info.attributes.fused_op_id = iiit->second.as<std::string>();
                } else if ((iiit->first.as<string>() == "granularity") && (op_info.type == "splice")) {
                    op_info.attributes.splice_mode =
                        netlist_utils::get_valid_splice_mode(iiit->second.as<std::string>());
                } else if (
                    (iiit->first.as<string>() == "num_indices") &&
                    (netlist_utils::is_valid_embedding_op(op_info.type))) {
                    op_info.attributes.num_indices = iiit->second.as<int>();
                } else if ((iiit->first.as<string>().find("input") == 0) && (op_info.type == "splice")) {
                    //
                    if (op_info.attributes.splice_infos.size() != op_info.input_names.size()) {
                        op_info.attributes.splice_infos.resize(op_info.input_names.size());
                    }
                    string input_string = iiit->first.as<string>();
                    int input_index = stoi(input_string.substr(input_string.find_first_of("0123456789")));
                    if (input_index >= 8) {
                        log_fatal(
                            " Max number of supported input attributes for splice op {} is 8!",
                            iiit->first.as<string>());
                    }
                    log_assert(
                        iiit->second.size() == 3,
                        "inputN={} attribute for op {} is used for splicing and must have 3 values [index, length, "
                        "stride]",
                        input_string,
                        op_info.name);
                    op_info.attributes.splice_infos.at(input_index) = {
                        .index = iiit->second[0].as<int>(),
                        .length = iiit->second[1].as<int>(),
                        .stride = iiit->second[2].as<int>(),
                    };

                } else if (iiit->first.as<string>() == "sfpu_execution_thread") {
                    if ((netlist_utils::is_valid_matmul_op(op_info.type) || (netlist_utils::get_valid_sfpu_op(op_info.type) != SfpuOp::Invalid))) {
                        op_info.attributes.sfpu_execution_thread = netlist_utils::get_sfpu_execution_thread(iiit->second.as<string>());
                        if (op_info.attributes.sfpu_execution_thread == SfpuExecutionThread::Unpack || op_info.attributes.sfpu_execution_thread == SfpuExecutionThread::Invalid) {
                            log_fatal("SFPU execution thread can only be math or pack, but is {}", iiit->second.as<string>());
                        }
                    } else {
                        log_fatal("SFPU execution thread attribute set for unsupported op {}", op_info.type);
                    }
                } else if (
                    (iiit->first.as<string>() == "requant") &&
                    (op_info.type == "matmul")) {
                    op_info.attributes.requant = iiit->second.as<bool>();
                } else if (
                    (iiit->first.as<string>() == "dequant") &&
                    (op_info.type == "matmul")) {
                    op_info.attributes.dequant = iiit->second.as<bool>();
                } else if (
                    (iiit->first.as<string>() == "k") &&
                    (op_info.type == "topk")) {
                    op_info.attributes.k = iiit->second.as<int>();
                } else if (
                    (iiit->first.as<string>() == "sort") &&
                    (op_info.type == "topk")) {
                    op_info.attributes.sort = netlist_utils::get_topk_sort_enum(iiit->second.as<string>());
                }else if (iiit->first.as<string>() == "zero_point") {
                    op_info.attributes.zero_point = iiit->second.as<float>();
                } else if ((iiit->first.as<string>() == "ingress_channels")) {
                    parse_op_attribute_ingress_channels(op_info, iiit->second);
                } else if ((iiit->first.as<string>() == "egress_channels")) {
                    parse_op_attribute_egress_channels(op_info, iiit->second);
                } else if ((iiit->first.as<string>() == "dest_device")) {
                    parse_op_attribute_dest_device(op_info, iiit->second);
                } else {
                    log_fatal(
                        "Attribute {} is invalid or unsupported for op {}, type {}",
                        iiit->first.as<string>(),
                        op_info.name,
                        op_info.type);
                }
            }
        }
    }

    // add arch info to enable device specific features
    op_info.arch_name = this->device_info.arch;
}
void netlist_parser::parse_tm_info(
    const YAML::Node &tms,
    const string &consumer_name,
    const string &consumer_type,
    const int &relative_input_index,
    bool &transpose,
    tt_tm_info &tm_info,
    std::vector<tt_input_pad_info> &input_padding,
    const bool fused_op_mode,
    const bool int32_on_input0) {
    if ((consumer_type == "queue") && (relative_input_index != 0)) {
        log_fatal(
            "parse_tm_info called for queue {} with input index = {}, a queue can have only one input",
            consumer_name,
            relative_input_index);
    }
    tm_info.insert({relative_input_index, {}});
    for (const auto tm_it : tms) {
        std::string tm_name = "";
        std::vector<int> tm_args = {};

        if (tm_it.Type() == YAML::NodeType::Scalar) {
            tm_name = tm_it.as<string>();
            if (tm_name == "transpose") {
                if (consumer_type == "matmul") {
                    if (!(relative_input_index == 1)) {
                        log_fatal(
                            "Transpose TM op is only supported on matmul input 1! Found on {}, type {}, "
                            "input {}",
                            consumer_name,
                            consumer_type,
                            relative_input_index);
                    }
                    transpose ^= true;
                } else if (consumer_type == "nop" or consumer_type == "datacopy" or is_valid_sfpu_op(consumer_type)) {
                    log_assert(!int32_on_input0,  "Transpose for Int32 input is not supported for {} operation.", consumer_type);
                    transpose ^= true;
                } else if (
                    (relative_input_index == 0) and is_valid_binary_op(consumer_type)) {
                    log_assert(!int32_on_input0,  "Transpose for Int32 input is not supported for {} operation.", consumer_type);
                    if (this->device_info.arch == tt::ARCH::GRAYSKULL) {
                        log_fatal(
                            "Transpose TM op on binary input 0 is not supported on GRAYSKULL arch! Found on op "
                            "{}, type {}, input {}",
                            consumer_name,
                            consumer_type,
                            relative_input_index);

                    }
                    transpose ^= true;
                } else {
                    log_fatal(
                        "Transpose TM op can be done only on matmul input 1, unary input or binary (non int32) input 0 (post GRAYSKULL arch)! Found on "
                        "{}, type {}, input {}",
                        consumer_name,
                        consumer_type,
                        relative_input_index);
                }
                tm_info.at(relative_input_index).push_back(std::make_pair(tm_name, tm_args));
            } else {
                log_fatal(
                    "Only Transpose TM op can be listed as a scalar value in tms! Found on {}, type {}, "
                    "input {}",
                    consumer_name,
                    consumer_type,
                    relative_input_index);
            }
        } else {
            for (YAML::const_iterator iiit = tm_it.begin(); iiit != tm_it.end(); ++iiit) {
                tm_name = iiit->first.as<string>();
                std::string final_tm_name = "";
                int tm_op_arg = 0;
                std::vector<int> tm_args = {};
                if (tm_name == "broadcast") {
                    for (YAML::const_iterator iiiit = iiit->second.begin(); iiiit != iiit->second.end(); ++iiiit) {
                        string broadcast_dim = iiiit->first.as<string>();
                        if (!((broadcast_dim == "r") or (broadcast_dim == "c") or (broadcast_dim == "z"))) {
                            log_fatal(
                                "Broadcast TM op only supports dims r/c/z! Found on {}, type {}, input {}",
                                consumer_name,
                                consumer_type,
                                relative_input_index);
                        }
                        final_tm_name = broadcast_dim + "_broadcast";
                        tm_args.push_back(iiiit->second.as<int>());
                        tm_info.at(relative_input_index).push_back(std::make_pair(final_tm_name, tm_args));
                    }
                } else if (tm_name == "tile_broadcast") {
                    log_assert(fused_op_mode, "tile_broadcast is only allowed in fused ops");
                    tm_args.push_back(static_cast<int>(netlist_utils::get_dim_enum(iiit->second.as<std::string>())));
                    tm_info.at(relative_input_index).push_back(std::make_pair(tm_name, tm_args));
                } else if (tm_name == "pad") {
                    log_assert(not fused_op_mode, "pad not allowed in fused ops");
                    auto& padding_info = input_padding.at(relative_input_index);
                    log_assert(consumer_type != "queue", "Padding can't be done on queue inputs: {}", consumer_name);
                    log_assert(not padding_info.default_padding and not padding_info.tm_padding, "Padding must be specified once: {}, type {}, input {}",
                        consumer_name,
                        consumer_type,
                        relative_input_index
                    );

                    padding_info.tm_padding = true;
                    int pad_tm_arg_counter = 0;
                    for(const auto& yaml_tm_arg: iiit->second) {
                        if (pad_tm_arg_counter < 2) {
                            tm_args.push_back(yaml_tm_arg.as<int>());
                        }
                        else {
                            padding_info.pad_value = yaml_tm_arg.as<float>();
                        }
                        pad_tm_arg_counter++;
                    }
                    log_assert(pad_tm_arg_counter == 3, "Padding TM must have 3 args: {}, type {}, input {}",
                        consumer_name,
                        consumer_type,
                        relative_input_index
                    );
                    tm_info.at(relative_input_index).push_back(std::make_pair(tm_name, tm_args));
                } else if (
                    (tm_name == "hstack") or (tm_name == "hslice") or (tm_name == "vstack") or (tm_name == "vslice")) {
                    final_tm_name = tm_name;
                    log_assert(
                        iiit->second.IsScalar(),
                        "{} needs to be a scalar value for {}", 
                        tm_name,
                        consumer_name
                    );
                    tm_op_arg = iiit->second.as<int>();
                    tm_args.push_back(tm_op_arg);
                    tm_info.at(relative_input_index).push_back(std::make_pair(final_tm_name, tm_args));
                } else {
                    log_fatal(
                        "Unsupported TM op {}! Found on {}, type {}, input {}",
                        tm_name,
                        consumer_name,
                        consumer_type,
                        relative_input_index);
                }
            }
        }
    }
}

int netlist_parser::get_tm_index(
    const string &tm_key, const string &op_name, const string &op_type, const int &num_inputs) {
    // Syntax check for tm_key
    auto num_pos1 = tm_key.find_first_of('_');
    if (!(num_pos1 != std::string::npos)) {
        log_fatal(
            "Invalid TM op syntax {}! Found on op {}, type {}. Missing '_' before input id",
            tm_key,
            op_name,
            op_type);
    }
    auto num_pos2 = tm_key.find_last_of('_');
    if (!(num_pos2 != num_pos1)) {
        log_fatal(
            "Invalid TM op syntax: {}! Found on op {}, type {}. Missing '_' after input id",
            tm_key,
            op_name,
            op_type);
    }
    // Get input id
    std::uint32_t relative_input_index = stoi(tm_key.substr(num_pos1 + 1, num_pos2 - 1));

    // Check if input is valid e.g. we are doing tm on existing input
    if (!(relative_input_index < num_inputs)) {
        log_fatal(
            "Invalid input {} in TM op syntax! Found on op {}, type {} which has {} valid inputs",
            relative_input_index,
            op_name,
            op_type,
            num_inputs);
    }
    return relative_input_index;
}


void netlist_parser::parse_queue_tm_ops(const YAML::Node& tms, const string &tm_key, tt_queue_info &queue_info) {
    std::vector<tt_input_pad_info> tmp_padding; // discarded - can't add padding on queue input
    bool tmp_transpose;
    parse_tm_info(
        tms,
        queue_info.name,
        "queue",
        0,
        tmp_transpose,
        queue_info.input_tm_ops,
        tmp_padding,
        false);
}


void netlist_parser::parse_tm_ops(const YAML::Node &tms, const string &tm_key, tt_op_info &op_info) {
    bool int32_on_input0 = op_info.input_data_formats.at(0) == DataFormat::Int32;
    parse_tm_info(
        tms,
        op_info.name,
        op_info.type,
        get_tm_index(tm_key, op_info.name, op_info.type, op_info.input_names.size()),
        op_info.transpose,
        op_info.input_tm_ops,
        op_info.input_padding,
        false,
        int32_on_input0);
}

void netlist_parser::parse_padding(const YAML::Node& input_padding_node, const string &input_key, tt_op_info &op_info) {
    const int input_index = get_tm_index(input_key, op_info.name, op_info.type, op_info.input_names.size());
    log_assert(not op_info.input_padding.at(input_index).tm_padding or 
                (op_info.input_padding.at(input_index).rt == 0 and op_info.input_padding.at(input_index).ct == 0), 
                "Padding must be specified only once. Op: {}, type {}, input {}", 
                op_info.name, op_info.type, input_index
            );
    for (const auto & it : input_padding_node) {
        std::string key = it.first.as<std::string>();
        auto value_node = it.second;
        if(key == "rt") {
            op_info.input_padding.at(input_index).rt = value_node.as<int>();
        }
        else if(key == "ct") {
            op_info.input_padding.at(input_index).ct = value_node.as<int>();
        }
        else if(key == "pad_value") {
            op_info.input_padding.at(input_index).pad_value = value_node.as<float>();
        }
        else {
            log_fatal(
                "Field {} is not supported for padding",
                key);
        }
    }
}

void netlist_parser::parse_unpadding(const YAML::Node& input_unpadding_node, const string &input_key, tt_op_info &op_info) {
    const int input_index = get_tm_index(input_key, op_info.name, op_info.type, op_info.input_names.size());
    for (const auto & it : input_unpadding_node) {
        std::string key = it.first.as<std::string>();
        auto value_node = it.second;
        if(key == "rt") {
            op_info.input_unpadding.at(input_index).rt = value_node.as<int>();
        }
        else if(key == "ct") {
            op_info.input_unpadding.at(input_index).ct = value_node.as<int>();
        }
        else {
            log_fatal(
                "Field {} is not supported for padding",
                key);
        }
    }
}

void netlist_parser::parse_graphs(const YAML::Node& graphs, unordered_map<string, tt_graph_info> &graph_map) {
    for (YAML::const_iterator graphs_it = graphs.begin(); graphs_it != graphs.end(); ++graphs_it) {
        string name = graphs_it->first.as<string>();
        const YAML::Node& graph = graphs_it->second;
        int pre_insert_size = graph_map.size();
        graph_map[name] = tt_graph_info();
        graph_map[name].name = name;
        for (YAML::const_iterator it = graph.begin(); it != graph.end(); ++it) {
            if (it->first.as<std::string>() == "target_device") {
                if (it->second.IsSequence()) {
                    for (const YAML::Node &node : it->second) {
                        graph_to_devices_map[name].push_back(node.as<int>());
                    }
                } else {
                    graph_map[name].target_device = it->second.as<int>();
                }
            } else if (it->first.as<std::string>() == "input_count") {
                graph_map[name].input_count = it->second.as<int>();
            } else {
                std::map<string, tt_op_info> &op_map = graph_map[name].op_map;
                int pre_insert_size = op_map.size();
                string op_name = it->first.as<string>();
                log_assert(op_graph_map.find(op_name) == op_graph_map.end(), "Error: duplicate op name {} found", op_name);
                op_graph_map[op_name] = name;
                op_map[op_name] = tt_op_info();
                // iterates through op parameters
                op_map[op_name].name = op_name;
                parse_op(it->second, op_map[op_name]);
                op_map[op_name].is_input_queue.resize(op_map[op_name].input_names.size());
                int index = 0;
                for (const auto &input_name : op_map[op_name].input_names) {
                    producer_consumer_map[input_name].insert(op_name);
                    bool is_input_queue = queue_map.find(input_name) != queue_map.end();
                    op_map[op_name].has_queue_inputs |= is_input_queue;
                    op_map[op_name].is_input_queue[index++] = is_input_queue;
                }
                log_assert(
                    op_map.size() > (uint32_t)pre_insert_size,
                    "Error: failed to insert op into op map, probably due to duplicate name, for graph name {} and op "
                    "name {}",
                    name,
                    op_name);
            }
        }
        log_assert(
            graph_map.size() > (uint32_t)pre_insert_size,
            "Error: failed to insert graph into graph map, probably due to duplicate name, for graph name {}",
            name);
        graph_order.push_back(name);
    }
}

void netlist_parser::parse_queue_settings(const YAML::Node& queue_settings, tt_instruction_info &instrn) {
    for (YAML::const_iterator it = queue_settings.begin(); it != queue_settings.end(); ++it) {
        tt_queue_setting_info q_setting;
        q_setting.name = it->first.as<std::string>();
        for (YAML::const_iterator iti = it->second.begin(); iti != it->second.end(); ++iti) {
            if (iti->first.as<std::string>() == "prologue") {
                q_setting.prolog = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "epilogue") {
                q_setting.epilog = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "zero") {
                q_setting.zero = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "rd_ptr_local") {
                q_setting.rd_ptr_local = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "rd_ptr_global") {
                q_setting.rd_ptr_global = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "wr_ptr_global") {
                q_setting.wr_ptr_global = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "global_rdptr_autoinc") {
                q_setting.global_rdptr_autoinc = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "rd_ptr_autoinc") {
                q_setting.rd_ptr_autoinc = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "global_wrptr_autoinc") {
                q_setting.global_wrptr_autoinc = iti->second.as<string>();
            } else if (iti->first.as<std::string>() == "read_only") {
                q_setting.read_only = iti->second.as<string>();
            } else {
                log_fatal(
                    "Field {} is not supported for tt_instruction_info::queue settings",
                    iti->first.as<std::string>());
            }
        }
        instrn.queue_settings.push_back(q_setting);
    }
}

void netlist_parser::parse_execute_instruction(const YAML::Node& execute_instruction, tt_instruction_info &instrn) {
    for (YAML::const_iterator it = execute_instruction.begin(); it != execute_instruction.end(); ++it) {
        if (it->first.as<std::string>() == "graph_name") {
            instrn.graph_name = it->second.as<std::string>();
        } else if (it->first.as<std::string>() == "queue_settings") {
            parse_queue_settings(it->second, instrn);
        } else {
            log_fatal(
                "Field {} is not supported for execute tt_instruction_info settings",
                it->first.as<std::string>());
        }
    }
}

void netlist_parser::parse_program(const YAML::Node& program, tt_program_info &program_info) {
    for (const auto &instruction_node : program) {
        tt_instruction_info instrn;
        if (instruction_node.IsScalar()) {
            instrn.opcode = get_instruction_opcode_enum(instruction_node.as<std::string>());
            if (instrn.opcode == INSTRUCTION_OPCODE::EndLoop) {
                instrn.graph_name = "not applicable";
            } else {
                log_fatal(
                    "Invalid OP Code={} parsed in program instructions as a scalar",
                    instruction_node.as<std::string>());
            }
        } else if (instruction_node.IsMap()) {
            if (instruction_node.size() != 1) {
                log_fatal("Each Instruction must be a map with only 1 entry and opcode as key");
            }
            for (YAML::const_iterator instr_map_it = instruction_node.begin(); instr_map_it != instruction_node.end();
                 ++instr_map_it) {
                instrn.opcode = get_instruction_opcode_enum(instr_map_it->first.as<std::string>());
                if (instrn.opcode == INSTRUCTION_OPCODE::Loop) {
                    instrn.graph_name = "not applicable";
                    instrn.loop_count = instr_map_it->second.as<string>();
                } else if (instrn.opcode == INSTRUCTION_OPCODE::Execute) {
                    parse_execute_instruction(instr_map_it->second, instrn);
                } else if (
                    (instrn.opcode == INSTRUCTION_OPCODE::Var) or (instrn.opcode == INSTRUCTION_OPCODE::StaticVar)) {
                    if (instr_map_it->second.IsMap()) {
                        for (YAML::const_iterator var_node = instr_map_it->second.begin();
                             var_node != instr_map_it->second.end();
                             ++var_node) {
                            instrn.vars.push_back({var_node->first.as<std::string>(), var_node->second.as<int>()});
                        }
                    } else if (instr_map_it->second.IsSequence()) {
                        instrn.is_declaration = true;
                        for (const YAML::Node &node : instr_map_it->second) {
                            instrn.vars.push_back({node.as<string>(), 0});
                        }
                    } else {
                        log_fatal(
                            "Declaration of variables must be formated as a list or map {}:{}",
                            instr_map_it->first,
                            instr_map_it->second);
                    }
                    if (instrn.vars.size() == 0) {
                        log_warning(tt::LogNetlist, "Empty variables declaration...");
                    }
                } else if (instrn.opcode == INSTRUCTION_OPCODE::Param) {
                    if (instr_map_it->second.IsSequence()) {
                        auto iter = instr_map_it->second.begin();
                        auto end = instr_map_it->second.end();
                        for (; iter != end; ++iter) {
                            instrn.vars.push_back({iter->as<string>(), 0});
                        }
                    } else {
                        log_fatal(
                            "Declaration of variables must be formated as a list {}:{}",
                            instr_map_it->first,
                            instr_map_it->second);
                    }
                    if (instrn.vars.size() == 0) {
                        log_warning(tt::LogNetlist, "Empty variables declaration...");
                    }
                } else if (instrn.opcode == INSTRUCTION_OPCODE::VarInst) {
                    log_assert(
                        instr_map_it->second.size() >= 2,
                        "VarInst must have at least 2 fields, varinst: [$out, <op_code>, <args>]");
                    instrn.varinst_opcode = get_var_instruction_opcode_enum(instr_map_it->second[1].as<string>());
                    instrn.vars.push_back({instr_map_it->second[0].as<string>(), 0});
                    for (uint32_t index = 2; index < instr_map_it->second.size(); index++) {
                        instrn.vars.push_back({instr_map_it->second[index].as<string>(), 0});
                    }
                } else if (
                    (instrn.opcode == INSTRUCTION_OPCODE::AllocateQueue) or
                    (instrn.opcode == INSTRUCTION_OPCODE::DeallocateQueue)) {
                    if (instr_map_it->second.IsSequence()) {
                        for (const YAML::Node &node : instr_map_it->second) {
                            std::string queue_name = node.as<string>();
                            log_assert(
                                queue_map.find(queue_name) != queue_map.end(),
                                "Queue={} being allocated/deallocated does not exist in the netlist!",
                                queue_name);
                            instrn.vars.push_back({queue_name, 0});
                        }
                    } else {
                        log_fatal(
                            "Alloc/dealloc of queues must be formated as a list {}:{}",
                            instr_map_it->first,
                            instr_map_it->second);
                    }
                } else if (instrn.opcode == INSTRUCTION_OPCODE::EndLoop) {
                    instrn.graph_name = "not applicable";
                } else {
                    log_fatal(
                        "Invalid OP Code={} parsed in program instructions as a map",
                        instr_map_it->first.as<std::string>());
                }
            }
        } else {
            log_fatal(
                "Instruction must be a scalar with instruction opcode or a map with opcode as key");
        }
        program_info.program_trace.push_back(instrn);
    }
    program_info.program_trace.push_back({.opcode = INSTRUCTION_OPCODE::EndProgram, .graph_name = "not applicable"});
}

void netlist_parser::parse_programs(const YAML::Node& programs, unordered_map<string, tt_program_info> &program_map) {
    for (const YAML::Node &program : programs) {
        log_assert(program.size() == 1, "Each entry in programs list must have only 1 map specified");
        for (const auto &program_it : program) {
            // Program is detected
            program_map[program_it.first.as<std::string>()] = {};
            program_map[program_it.first.as<std::string>()].name = program_it.first.as<std::string>();
            parse_program(program_it.second, program_map[program_it.first.as<std::string>()]);
            program_order.push_back(program_it.first.as<std::string>());
        }
    }
}

void netlist_parser::parse_scheduled_op_fields(
    const YAML::Node& op_fields, const string &op_name, tt_scheduled_op_info &scheduled_op_info) {
    log_assert(op_fields.IsMap(), "op_fields must be a map");
    scheduled_op_info.name = op_name;
    scheduled_op_info.output_dim.t = 1;
    scheduled_op_info.output_dim.ublock_order = Dim::R;
    for (const auto &op_fields_it : op_fields) {
        if (op_fields_it.first.as<std::string>() == "type") {
            scheduled_op_info.type = op_fields_it.second.as<std::string>();
        } else if (op_fields_it.first.as<std::string>() == "inputs") {
            scheduled_op_info.input_names = op_fields_it.second.as<std::vector<std::string>>();
        } else if (op_fields_it.first.as<std::string>() == "output") {
            scheduled_op_info.output = op_fields_it.second.as<std::string>();
        } else if (op_fields_it.first.as<std::string>() == "mblock") {
            log_assert(op_fields_it.second.size() == 2, "mblock must contain 2 values");
            scheduled_op_info.output_dim.mblock_m = op_fields_it.second[0].as<int>();
            scheduled_op_info.output_dim.mblock_n = op_fields_it.second[1].as<int>();
        } else if (op_fields_it.first.as<std::string>() == "ublock") {
            log_assert(op_fields_it.second.size() == 2, "ublock must contain 2 values");
            scheduled_op_info.output_dim.ublock_rt = op_fields_it.second[0].as<int>();
            scheduled_op_info.output_dim.ublock_ct = op_fields_it.second[1].as<int>();
        } else if (op_fields_it.first.as<std::string>() == "pop") {
            for (const auto &input_to_pop_it : op_fields_it.second) {
                auto input_to_pop = input_to_pop_it.as<std::string>();
                log_assert(
                    scheduled_op_info.inputs_to_pop.find(input_to_pop) == scheduled_op_info.inputs_to_pop.end(),
                    "input_to_pop={} is duplicated in the list to pop",
                    input_to_pop);
                // log_assert(
                //     input_to_pop.find("intermed") == 0,
                //     "input_to_pop={} must be an intermediate buffer in format intermedX",
                //     input_to_pop);
                scheduled_op_info.inputs_to_pop.insert(input_to_pop);
            }
        } else if (op_fields_it.first.as<std::string>() == "pop_last") {
            for (const auto &input_to_pop_last_it : op_fields_it.second) {
                auto input_to_pop_last = input_to_pop_last_it.as<std::string>();
                log_assert(
                    scheduled_op_info.inputs_to_pop_last.find(input_to_pop_last) ==
                        scheduled_op_info.inputs_to_pop_last.end(),
                    "input_to_pop_last={} is duplicated in the list to pop",
                    input_to_pop_last);
                log_assert(
                    input_to_pop_last.find("intermed") == 0,
                    "input_to_pop_last={} must be an intermediate buffer in format intermedX",
                    input_to_pop_last);
                scheduled_op_info.inputs_to_pop_last.insert(input_to_pop_last);
            }
        } else if (op_fields_it.first.as<std::string>() == "zero_point") {
            scheduled_op_info.zero_point = op_fields_it.second.as<float>();
        } else if (
            (op_fields_it.first.as<std::string>().find("tms") != std::string::npos) and
            (op_fields_it.first.as<std::string>().find("input") != std::string::npos)) {
            continue;
        } else if (op_fields_it.first.as<std::string>() == "attributes") {
            for (const auto &attributes_it : op_fields_it.second) {
                if (attributes_it.first.as<std::string>() == "m_k") {
                    scheduled_op_info.m_k = attributes_it.second.as<int>();
                } else if (attributes_it.first.as<std::string>() == "u_kt") {
                    scheduled_op_info.u_kt = attributes_it.second.as<int>();
                } else if (attributes_it.first.as<std::string>() == "vector") {
                    scheduled_op_info.vector_mode = netlist_utils::get_dim_enum(attributes_it.second.as<string>());
                } else if ((attributes_it.first.as<std::string>() == "type") && (scheduled_op_info.type == "reduce")) {
                    scheduled_op_info.reduce_type =
                        netlist_utils::get_valid_reduce_func(attributes_it.second.as<string>());
                } else if ((attributes_it.first.as<std::string>() == "dim") && (scheduled_op_info.type == "reduce")) {
                    scheduled_op_info.reduce_dim = netlist_utils::get_dim_enum(attributes_it.second.as<string>());
                } else if ((attributes_it.first.as<std::string>() == "relu_en")) {
                    scheduled_op_info.relu_en = attributes_it.second.as<bool>();
                } else if ((attributes_it.first.as<std::string>() == "relu_threshold")) {
                    scheduled_op_info.relu_threshold = attributes_it.second.as<float>();
                    if (scheduled_op_info.relu_threshold < 0.0f) {
                        log_fatal("Relu threshold for {} op must be >= 0.0", scheduled_op_info.name);
                    }
                } else if ((attributes_it.first.as<std::string>() == "relu_mode")) {
                    scheduled_op_info.relu_mode =
                        netlist_utils::get_relu_mode_enum(attributes_it.second.as<std::string>());
                } else if (
                    (attributes_it.first.as<string>() == "p") &&
                    (scheduled_op_info.type == "dropout")) {
                    scheduled_op_info.probability = attributes_it.second.as<float>();
                    if ((scheduled_op_info.probability < 0.0f) || (scheduled_op_info.probability > 1.0f)) {
                        log_fatal(
                            "Probability attribute for {} op must be in [0.0:1.0] range", scheduled_op_info.name);
                    }
                    scheduled_op_info.scale =
                        scheduled_op_info.probability == 1.0 ? 1.0 : 1.0 / (1.0 - scheduled_op_info.probability);
                } else if (
                    (attributes_it.first.as<string>() == "seed") &&
                    (scheduled_op_info.type == "dropout")) {
                    scheduled_op_info.seed = attributes_it.second.as<int>();
                } else if (
                    (attributes_it.first.as<string>() == "slope") &&
                    (scheduled_op_info.type == "lrelu")) {
                    scheduled_op_info.slope = attributes_it.second.as<float>();
                } else if (
                    (attributes_it.first.as<string>() == "exp") &&
                    (scheduled_op_info.type == "power")) {
                    scheduled_op_info.exponent = attributes_it.second.as<int>();
                } else {
                    log_fatal(
                        "Field {} is not supported for tt_scheduled_op_info::attributes",
                        attributes_it.first.as<std::string>());
                }
            }
        } else {
            log_fatal(
                "Field {} is not supported for tt_scheduled_op_info",
                op_fields_it.first.as<std::string>());
        }
    }
    // tms may depend on other op info fields, so do this second
    bool transpose = false;
    for (const auto &op_fields_it : op_fields) {
        if ((op_fields_it.first.as<std::string>().find("tms") != std::string::npos) and
            (op_fields_it.first.as<std::string>().find("input") != std::string::npos)) {
            std::vector<tt_input_pad_info> dummy_padding_info;
            parse_tm_info(
                op_fields_it.second,
                scheduled_op_info.name,
                scheduled_op_info.type,
                get_tm_index(
                    op_fields_it.first.as<std::string>(),
                    scheduled_op_info.name,
                    scheduled_op_info.type,
                    scheduled_op_info.input_names.size()),
                transpose,  // op_info.transpose.. transpose not supported in fused_op
                scheduled_op_info.input_tm_ops,
                dummy_padding_info,
                true);
        }
    }
    log_assert(
        not transpose,
        "List of tm's used in op={} will cause a transpose at the tile level "
        "which is not supported in fused ops",
        scheduled_op_info.name);
}


void netlist_parser::parse_fused_ops(const YAML::Node& fused_ops) {
    log_assert(fused_ops.IsMap(), "Fused ops must be a map of ids to information");
    for (const auto &fused_op_it : fused_ops) {
        // Program is detected
        tt_fused_op_info fused_op = {.name = fused_op_it.first.as<std::string>()};
        const auto &fields = fused_op_it.second;
        for (const auto &fields_it : fields) {
            if (fields_it.first.as<std::string>() == "inputs") {
                fused_op.num_inputs = fields_it.second.as<int>();
            } else if (fields_it.first.as<std::string>() == "intermediates") {
                fused_op.num_intermediates = fields_it.second.as<int>();
            } else if (fields_it.first.as<std::string>() == "custom_kernel_path") {
                fused_op.custom_kernel_path = fields_it.second.as<std::string>();
            } else if (fields_it.first.as<std::string>() == "schedules") {
                const auto schedules = fields_it.second;
                log_assert(schedules.IsSequence(), "schedules must be a list schedules");
                for (const auto &schedule : schedules) {
                    tt_schedule_info schedule_info = {};
                    for (const auto &op_entry : schedule) {
                        log_assert(op_entry.IsMap(), "op must be a map");
                        log_assert(
                            (op_entry.size() == 1),
                            "Each op in the schedule list must be a map between the op name and fields or a "
                            "op_type: { field_name: field_value ... }");
                        tt_scheduled_op_info scheduled_op_info = {};
                        try {
                            for (const auto &op_field_it : op_entry) {
                                    parse_scheduled_op_fields(
                                        op_field_it.second, op_field_it.first.as<std::string>(), scheduled_op_info);
                            }
                            log_assert(
                                scheduled_op_info.name != "",
                                "Could not find an entry in parsed scheduled_op_info for the name:fields mapping");
                        } catch (const std::exception &e) {
                            log_error("{}", e.what());
                            log_fatal(
                                "Error parsing fused_op_id={}, scheduled_op={}",
                                fused_op.name,
                                scheduled_op_info.name);
                        }
                        schedule_info.scheduled_ops.push_back(scheduled_op_info);
                    }
                    fused_op.schedules.push_back(schedule_info);
                }
            }
        }
        fused_ops_unique_map.insert({fused_op.name, fused_op});
    }
}
void netlist_parser::parse_string(string netlist) {
    YAML::Node yaml_netlist = YAML::Load(netlist);
    parse_yaml(yaml_netlist);
}

void netlist_parser::parse_file(string file) {
    log_debug(tt::LogNetlist, "Parsing Netlist from file: {}", file);
    try {
        YAML::Node netlist = YAML::LoadFile(file);
        parse_yaml(netlist);
    } catch (const std::exception &e) {
        log_fatal("{}", e.what());
    }
}

void netlist_parser::parse_yaml(const YAML::Node& netlist) {
    if (initialized) {
        clear();
    }
    for (YAML::const_iterator it = netlist.begin(); it != netlist.end(); ++it) {
        if (it->first.as<std::string>() == "devices") {
            try {
                parse_devices(it->second, device_info);
            } catch (const std::exception &e) {
                log_fatal("{}", e.what());
            }
        }
        if (it->first.as<std::string>() == "queues") {
            try {
                parse_queues(it->second, queue_map);
            } catch (const std::exception &e) {
                log_fatal("{}", e.what());
            }
        }
        if (it->first.as<std::string>() == "graphs") {
            try {
                parse_graphs(it->second, graph_map);
            } catch (const std::exception &e) {
                log_fatal("{}", e.what());
            }
        }
        if (it->first.as<std::string>() == "programs") {
            try {
                parse_programs(it->second, program_map);
            } catch (const std::exception &e) {
                log_fatal("{}", e.what());
            }
        }
        if (it->first.as<std::string>() == "fused_ops") {
            try {
                parse_fused_ops(it->second);
            } catch (const std::exception &e) {
                log_fatal("{}", e.what());
            }
        }
    }
    try {
        expand_multi_instance_structures();
    } catch (const std::exception &e) {
        log_fatal("{}", e.what());
    }
    try {
        derive_complex_settings();
    } catch (const std::exception &e) {
        log_fatal("{}", e.what());
    }

    verify(device_info);
    verify_queues();
    verify_graphs();
    verify_programs();
    verify_fused_ops();
    verify_complex_settings();
    initialized = true;
}

void netlist_parser::verify_queues() {
    log_assert(queue_map.size(), "Queues empty for netlist parsed");
    log_trace(tt::LogNetlist, "Queues Parsed In Netlist");
    for (auto it : queue_map) {
        log_trace(tt::LogNetlist, "Queue {}: \n{}", it.first, it.second);
        try {
            verify(it.second);
        } catch (const std::exception &e) {
            log_error("{}", e.what());
            log_fatal("Error parsing netlist for Queue {}: \n{}", it.first, it.second);
        }
    }
}
void netlist_parser::verify_graphs() {
    log_assert(graph_map.size(), "Graphs empty for netlist parsed");
    log_trace(tt::LogNetlist, "Graph Info Parsed In Netlist:");
    for (auto it : graph_map) {
        log_trace(tt::LogNetlist, "Graph {}: \n{}", it.first, it.second);
        try {
            verify(it.second);
        } catch (const std::exception &e) {
            log_error("{}", e.what());
            log_fatal("Error parsing netlist for Graph {}: \n{}", it.first, it.second);
        }
    }
}
void netlist_parser::verify_programs() {
    log_assert(program_map.size(), "Programs empty for netlist parsed");
    log_trace(tt::LogNetlist, "Program Info Parsed In Netlist:");
    for (auto it : program_map) {
        log_trace(tt::LogNetlist, "Program {}: \n{}", it.first, it.second);
        try {
            verify(it.second);
        } catch (const std::exception &e) {
            log_error("{}", e.what());
            log_fatal("Error parsing netlist for Program {}: \n{}", it.first, it.second);
        }
    }
    for (auto it : program_order) {
        if (program_map.find(it) == program_map.end()) {
            log_fatal("Program in program order isn't defined: {}", it);
        }
    }
}
void netlist_parser::verify_fused_ops() {
    log_trace(tt::LogNetlist, "Fused Ops Parsed In Netlist:");
    for (auto it : fused_ops_op_map) {
        log_trace(tt::LogNetlist, "Fused Ops {}: \n{}", it.first, it.second);
        try {
            verify(it.second, device_info.arch);
        } catch (const std::exception &e) {
            log_error("{}", e.what());
            log_fatal("Error parsing netlist for Fused Ops {}: \n{}", it.first, it.second);
        }
    }
}

const std::unordered_set<std::string> &netlist_parser::get_graphs_of_temporal_graph(
    temporal_graph_id_t temporal_graph) const {
    return this->temporal_graph_graphs.at(temporal_graph);
}

temporal_graph_id_t netlist_parser::get_temporal_graph_of_graph(const std::string &graph_name) const {
    return this->graph_temporal_graphs.at(graph_name);
}

void netlist_parser::compress_temporal_graph_ids() {
    // graph_temporal_graphs
    auto target_temporal_graph_id = std::begin(this->temporal_graph_graphs);

    for (auto input_temporal_graph_id = std::begin(this->temporal_graph_graphs);
         input_temporal_graph_id != std::end(this->temporal_graph_graphs);
         ++input_temporal_graph_id) {
        const auto graphs = *input_temporal_graph_id;
        auto target_index = std::distance(std::begin(this->temporal_graph_graphs), target_temporal_graph_id);
        if (graphs.size() > 0) {
            bool src_and_target_indices_differ = input_temporal_graph_id != target_temporal_graph_id;
            if (src_and_target_indices_differ) {
                const auto &input_temporal_graph_index =
                    std::distance(std::begin(this->temporal_graph_graphs), input_temporal_graph_id);
                if (target_temporal_graph_id->size() > 0) {
                    log_fatal(
                        "temporal graph ID compression failed. Tried to compress ID {} to ID {}, but ID {} wasn't "
                        "shifted to an earlier ID and made available",
                        input_temporal_graph_index,
                        target_index,
                        target_index);
                }
                target_temporal_graph_id->merge(*input_temporal_graph_id);

                log_debug(
                    tt::LogNetlist,
                    "Moving the following graphs from temporal graph ID {} to temporal graph ID {}",
                    input_temporal_graph_index,
                    target_index);
                for (const auto &graph : graphs) {
                    log_debug(tt::LogNetlist, "\t{}", graph);
                    this->graph_temporal_graphs[graph] = target_index;
                }

                if (input_temporal_graph_id->size() > 0) {
                    log_fatal(
                        "temporal graph ID compression failed. After compressing graphs from temporal graph ID {} to "
                        "ID {}, all subgraphs weren't moved",
                        input_temporal_graph_index,
                        target_index);
                }
            }

            ++target_temporal_graph_id;
        }
    }

    this->temporal_graph_graphs.erase(target_temporal_graph_id, std::end(this->temporal_graph_graphs));
}


void netlist_parser::merge_non_overlapping_adjacent_temporal_epochs() {
    if (this->device_info.arch == tt::ARCH::GRAYSKULL) {
        return;
    }

    auto temporal_epochs_overlap_devices = [this](int last_temporal_epoch, int this_temporal_epoch) {
        auto last_temporal_epoch_devices = std::set<int>();
        auto this_temporal_epoch_devices = std::set<int>();

        for (const auto &gname : temporal_graph_graphs.at(last_temporal_epoch)) {
            last_temporal_epoch_devices.insert(this->graph_map.at(gname).target_device);
        }
        for (const auto &gname : temporal_graph_graphs.at(this_temporal_epoch)) {
            this_temporal_epoch_devices.insert(this->graph_map.at(gname).target_device);
        }

        for (auto chip_id : last_temporal_epoch_devices) {
            if (this_temporal_epoch_devices.find(chip_id) != this_temporal_epoch_devices.end()) {
                return true;
            }
        }

        return false;
    };

    int last_temporal_epoch = -1;
    for (const auto &[program_name, program] : this->program_map) {
        for (int i = 0; i < program.program_trace.size(); i++) {
            const auto &instruction = program.program_trace.at(i);
            if (instruction.opcode == INSTRUCTION_OPCODE::Execute) {
                if (last_temporal_epoch == -1) {
                    last_temporal_epoch = this->graph_temporal_graphs.at(instruction.graph_name);
                }
                int this_temporal_epoch = this->graph_temporal_graphs.at(instruction.graph_name);

                bool different_temporal_epoch = this_temporal_epoch != last_temporal_epoch;
                if (different_temporal_epoch) {
                    bool merge_temporal_epochs =
                        !temporal_epochs_overlap_devices(last_temporal_epoch, this_temporal_epoch);
                    if (merge_temporal_epochs) {
                        for (const auto &gname : this->temporal_graph_graphs.at(this_temporal_epoch)) {
                            this->graph_temporal_graphs[gname] = last_temporal_epoch;
                            this->temporal_graph_graphs.at(last_temporal_epoch).insert(gname);
                        }
                        this->temporal_graph_graphs.at(this_temporal_epoch).clear();
                        this_temporal_epoch = last_temporal_epoch;
                    }
                }

                last_temporal_epoch = this_temporal_epoch;
            }
        }
    }
}

void netlist_parser::derive_temporal_graphs() {
    // derive temporal_graph mappings

    if (this->temporal_graph_graphs.size() > 0 || this->graph_temporal_graphs.size() > 0) {
        log_fatal("temporal_graph_graphs and graph_temporal_graph double initialized");
    }

    auto op_graph_map = std::unordered_map<std::string, std::string>();
    for (const auto &[graph_name, graph] : this->graph_map) {
        for (const auto &[op_name, op_info] : graph.op_map) {
            op_graph_map.insert({op_name, graph_name});
        }
    }

    int temporal_graph = 0;
    for (const auto &graph_name : this->graph_order) {
        const auto &graph = this->graph_map.at(graph_name);
        bool graph_already_assigned_temporal_graph =
            this->graph_temporal_graphs.find(graph_name) != this->graph_temporal_graphs.end();
        temporal_graph_id_t my_temporal_graph =
            graph_already_assigned_temporal_graph ? this->graph_temporal_graphs.at(graph_name) : temporal_graph;
        if (not graph_already_assigned_temporal_graph) {
            this->graph_temporal_graphs[graph_name] = my_temporal_graph;
            if (this->temporal_graph_graphs.size() <= (uint32_t)my_temporal_graph) {
                this->temporal_graph_graphs.resize(my_temporal_graph + 1);
            }
            this->temporal_graph_graphs[my_temporal_graph].insert(graph_name);
            temporal_graph += 1;
        }

        std::unordered_set<std::string> producer_graph_names = {};

        for (const auto &[op_name, op_info] : graph.op_map) {
            int input_index = 0;
            for (const auto &input : op_info.input_names) {
                bool input_is_queue = op_info.is_input_queue[input_index++];
                bool input_is_op_in_graph = (graph.op_map.find(input) != graph.op_map.end());
                if ((not input_is_op_in_graph) and (not input_is_queue)) {
                    if (is_queueless_multichip_supported(device_info.arch)) {
                        const auto &input_graph_name = op_graph_map.at(input);
                        producer_graph_names.insert(input_graph_name);

                    } else {
                        log_fatal(
                            "input={} to op={} must be either a queue, or an op within the graph, cross graph "
                            "communication must be through queues/buffers",
                            input,
                            op_info.name);
                    }
                }
            }
        }

        for (const auto &producer_graph_name : producer_graph_names) {
            bool producer_graph_mapped_to_temporal_graph =
                this->graph_temporal_graphs.find(producer_graph_name) != this->graph_temporal_graphs.end();
            bool merge_producer_graph_temporal_graph_to_mine =
                producer_graph_mapped_to_temporal_graph &&
                this->graph_temporal_graphs.at(producer_graph_name) != my_temporal_graph;
            if (merge_producer_graph_temporal_graph_to_mine) {
                auto &other_temporal_graph_graphs =
                    this->temporal_graph_graphs.at(this->graph_temporal_graphs.at(producer_graph_name));
                for (const auto &other_temporal_graph_graph : other_temporal_graph_graphs) {
                    this->graph_temporal_graphs[other_temporal_graph_graph] = my_temporal_graph;
                }
                this->temporal_graph_graphs[my_temporal_graph].merge(other_temporal_graph_graphs);
                bool duplicate_graph_names =
                    this->temporal_graph_graphs.at(this->graph_temporal_graphs.at(producer_graph_name)).size() == 0;
                bool producer_graph_temporal_graph_not_reassigned =
                    this->graph_temporal_graphs.at(producer_graph_name) != my_temporal_graph;
                if (duplicate_graph_names || producer_graph_temporal_graph_not_reassigned) {
                    log_fatal("duplicate_graph_names or producer_graph_temporal_graph_not_reassigned");
                }

            } else if (not producer_graph_mapped_to_temporal_graph) {  // assign producer graph temporal graph to mine
                this->graph_temporal_graphs[producer_graph_name] = my_temporal_graph;
                this->temporal_graph_graphs[my_temporal_graph].insert(producer_graph_name);
            }
        }
    }

    merge_non_overlapping_adjacent_temporal_epochs();

    log_trace(tt::LogNetlist, "Graph -> temporal graph map:");
    for ([[maybe_unused]] const auto &[graph_name, temporal_graph_id] : this->graph_temporal_graphs) {
        log_trace(tt::LogNetlist, "\t{} -> {}", graph_name, temporal_graph_id);
    }

    for ([[maybe_unused]] uint32_t e = 0; e < this->temporal_graph_graphs.size(); e++) {
        const auto &graph_names = this->temporal_graph_graphs.at(e);
        log_trace(tt::LogNetlist, "Temporal Graph {}", e);
        for ([[maybe_unused]] const auto &n : graph_names) {
            log_trace(tt::LogNetlist, "\t{}", n);
        }
    }

    compress_temporal_graph_ids();
}

void netlist_parser::derive_complex_settings() {
    // Derive default autoinc for each queue_setting if it is not been defined
    for (auto &program_it : program_map) {
        for (auto & instruction_it : program_it.second.program_trace) {
            for (auto& queue_setting_it : instruction_it.queue_settings) {
                log_assert(
                    queue_map.find(queue_setting_it.name) != queue_map.end(),
                    "Cannot for queue_setting for IO={} in Program={}", 
                    queue_setting_it.name, program_it.second.name
                );
                const auto& queue_info = queue_map.at(queue_setting_it.name);
                if (queue_info.type == IO_TYPE::Queue) {
                    // IO_TYPE::Queue
                    if (queue_setting_it.rd_ptr_autoinc.empty()) {
                        queue_setting_it.rd_ptr_autoinc = "1"; 
                    }
                    if (queue_setting_it.global_wrptr_autoinc.empty()) {
                        queue_setting_it.global_wrptr_autoinc = "1"; 
                    }
                    if (queue_setting_it.global_rdptr_autoinc.empty()) {
                        queue_setting_it.global_rdptr_autoinc = "0"; 
                    }
                } else if (queue_info.type == IO_TYPE::RandomAccess) {
                    // IO_TYPE::RandomAccess
                    if (queue_setting_it.rd_ptr_autoinc.empty()) {
                        queue_setting_it.rd_ptr_autoinc = "0"; 
                    }
                    if (queue_setting_it.global_wrptr_autoinc.empty()) {
                        queue_setting_it.global_wrptr_autoinc = "0"; 
                    }
                    if (queue_setting_it.global_rdptr_autoinc.empty()) {
                        queue_setting_it.global_rdptr_autoinc = "0"; 
                    }
                } else {
                    log_fatal ("Unsupported IO Type for IO={}", queue_setting_it.name);
                }
            }
        }
    }

    // Check the consumer -- This should always be used if it exists
    for (const auto &graph_it : graph_map) {  // Check the consumer
        for (const auto &op_it : graph_it.second.op_map) {
            if (op_it.second.has_queue_inputs) {
                for (int i = 0; i < op_it.second.input_names.size(); i++) {
                    if (op_it.second.is_input_queue[i]) {
                        tt_queue_info &queue_info = queue_map[op_it.second.input_names[i]];
                        queue_info.input_count = graph_it.second.input_count;
                    }
                }
            }
        }
    }

    // Derive Input-Count for each Queue
    for(auto& queue_it : queue_map) {
        string queue_name = queue_it.second.name;
        string producer_name = queue_it.second.input;
        bool param_or_constant = queue_it.second.type == IO_TYPE::RandomAccess and queue_it.second.entries == 1;
        if (param_or_constant) {
            queue_it.second.input_count = 1;
        } else {
            if (producer_name == "HOST") {
                // For host queue host producer, src device is the target device
                queue_it.second.src_device_id = queue_it.second.target_device;
            } else {
                // For host queues, set producer (source) op/graph's device_id on queue info since unique queue alloc
                // per device_id.
                if (queue_it.second.loc == QUEUE_LOCATION::HOST) {
                    for (const auto &graph_it : graph_map) {  // Check the consumer
                        for (const auto &op_it : graph_it.second.op_map) {
                            if (op_it.first == producer_name) {
                                queue_it.second.src_device_id = graph_it.second.target_device;
                            }
                        }
                    }
                }

                if (queue_it.second.input_count == -1) {
                    // Queue producer is Op and we haven't found a consumer across all graphs --> output queue
                    for (const auto &graph_it : graph_map) {
                        if (graph_it.second.op_map.find(producer_name) != graph_it.second.op_map.end()) {
                            queue_it.second.input_count = graph_it.second.input_count;
                        }
                    }
                }
            }
        }
    }

    this->derive_temporal_graphs();

    // derive input dims for each input of each op
    derive_input_dims_for_ops();

    // derive input tile sizes for each input of each op
    derive_input_tile_dims_for_ops();

    derive_vector_mode_for_ops();

    // Go through the graphs and the fused ops and link it with a copy of the fused_op_info
    for (auto &graph_it : graph_map) {
        for (auto &op_it : graph_it.second.op_map) {
            if (op_it.second.type == "fused_op") {
                const auto &fused_op_id = op_it.second.attributes.fused_op_id;
                tt_fused_op_info fused_op_info = fused_ops_unique_map.at(fused_op_id);
                log_assert(
                    fused_op_info.num_inputs == op_it.second.input_names.size(),
                    "fused_op_id={} has inputs={} must be same as specified in number of inputs={} in op={}, graph={}",
                    fused_op_id,
                    fused_op_info.num_inputs,
                    op_it.second.input_names.size(),
                    op_it.first,
                    graph_it.first);

                fused_op_info.input_data_formats = op_it.second.input_data_formats;
                fused_op_info.accumulation_data_format = op_it.second.dest_accumulate_data_format;
                fused_op_info.intermed_data_format = op_it.second.intermed_data_format;
                fused_op_info.output_data_format = op_it.second.output_data_format;

                // Derive the mapping of relative input names to actual_input_names
                for (int i = 0; i < op_it.second.input_names.size(); i++) {
                    string relative_name = "input" + to_string(i);
                    fused_op_info
                        .input_names_to_rel_names_map.insert({op_it.second.input_names.at(i), relative_name});
                }

                // Find all forked inputs
                for (int i = 0; i < op_it.second.input_names.size(); i++) {
                    string relative_name = "input" + to_string(i);
                    int input_used = 0;
                    int buff_size_scaler = 1;
                    int scheduled_op_m_k = 1;
                    bool schedule_has_matmul_or_reduce = false;
                    bool input_found = false;
                    bool first_schedule = true;
                    for (auto &schedule : fused_op_info.schedules) {
                        schedule_has_matmul_or_reduce = false;
                        for (auto &scheduled_op : schedule.scheduled_ops) {
                            if (std::find(
                                    scheduled_op.input_names.begin(), scheduled_op.input_names.end(), relative_name) !=
                                scheduled_op.input_names.end()) {
                                if (first_schedule) {
                                    first_schedule = false;
                                    input_found = true; // first schedule where input is found
                                } else {
                                    input_found = false;
                                }
                                input_used++;
                            }
                            if ((scheduled_op.type == "matmul") or (scheduled_op.type == "reduce")) {
                                schedule_has_matmul_or_reduce = true; // schedule has matmul or reduce op
                                scheduled_op_m_k = scheduled_op.m_k;
                            }
                        }
                        if (input_found && schedule_has_matmul_or_reduce) {
                            buff_size_scaler = scheduled_op_m_k;
                        }
                    }
                    if (input_used > 1) {
                        fused_op_info.forked_input_names[relative_name] = buff_size_scaler;
                    }
                }

                // Check if any input has exp_a format
                bool is_any_input_exp_a = false;
                for (auto &in_df : op_it.second.input_data_formats) {
                    if (netlist_utils::is_exp_a_format(in_df)) {
                        is_any_input_exp_a = true;
                        break;
                    }
                }

                is_any_input_exp_a |= netlist_utils::is_exp_a_format(op_it.second.intermed_data_format);
                const bool acc_df_float32 = op_it.second.dest_accumulate_data_format == DataFormat::Float32;

                // Check if any scheduled op is using intermed buffer.
                bool is_intermed_buffer_used = false;
                for (const auto &schedule : fused_op_info.schedules) {
                    for (const auto &scheduled_op : schedule.scheduled_ops) {
                        if (scheduled_op.output.find("intermed") != std::string::npos ||
                            netlist_utils::is_valid_matmul_op(scheduled_op.type) ||
                            netlist_utils::is_valid_reduce_op(scheduled_op.type)) {
                            is_intermed_buffer_used = true;
                            break;
                        }
                    }
                }

                // Check if output and intermed data formats are the same
                fused_op_info.output_and_intermed_format_match = is_intermed_buffer_used ? 
                    op_it.second.output_data_format == op_it.second.intermed_data_format : true;

                // Check if all inputs and intermed data formats are the same
                tt::DataFormat input_data_format = is_intermed_buffer_used ? op_it.second.intermed_data_format : op_it.second.input_data_formats[0];
                fused_op_info.inputs_and_intermed_formats_match = true;
                for (const auto& df : op_it.second.input_data_formats) {
                    if (df != tt::DataFormat::Invalid && df != input_data_format){
                        fused_op_info.inputs_and_intermed_formats_match = false;
                        break;
                    }
                }

                // Copy output data format and relu settings
                for (auto &schedule : fused_op_info.schedules) {
                    for (auto &scheduled_op : schedule.scheduled_ops) {
                        scheduled_op.output_data_format = op_it.second.output_data_format;
                        if (scheduled_op.output == "output") {
                            if (op_it.second.attributes.relu_en and scheduled_op.relu_en) {
                                log_assert(
                                    op_it.second.attributes.relu_threshold == scheduled_op.relu_threshold,
                                    "base op_info={} and scheduled_op={} both have relu_en set, but mismatching "
                                    "relu_threshold",
                                    op_it.second.name,
                                    scheduled_op.name);
                                log_assert(
                                    op_it.second.attributes.relu_mode == scheduled_op.relu_mode,
                                    "base op_info={} and scheduled_op={} both have relu_en set, but mismatching "
                                    "relu_mode",
                                    op_it.second.name,
                                    scheduled_op.name);
                            }
                            if (op_it.second.attributes.relu_en) {
                                scheduled_op.relu_en = op_it.second.attributes.relu_en;
                                scheduled_op.relu_threshold = op_it.second.attributes.relu_threshold;
                                scheduled_op.relu_mode = op_it.second.attributes.relu_mode;
                            }
                        }

                        // Enable fp32 to fp16_a conversion for movd2a/b (bbe bug #1372)
                        // Cast is needed in case acc_df == Float32, we have binary op 
                        // and one of the inputs is "dest" and other is of exp type A.
                        scheduled_op.cast_dest_fp32_to_fp16_a =
                            scheduled_op.input_names.size() == 2 && acc_df_float32 &&
                            (scheduled_op.input_names[0] == "dest" || scheduled_op.input_names[1] == "dest") &&
                            is_any_input_exp_a;
                    }
                }

                // Copy the kernel_broadcast vector to fused_op_map
                fused_op_info.kernel_broadcast = op_it.second.attributes.kernel_broadcast;

                // Fill in the information unique to each fused_op invocation at the graph
                for (auto &schedule : fused_op_info.schedules) {
                    for (auto &scheduled_op : schedule.scheduled_ops) {
                        scheduled_op.output_dim.t = op_it.second.output_dim.t;
                        scheduled_op.output_dim.ublock_order = op_it.second.output_dim.ublock_order;
                    }
                }

                // Find intermed buff size requirements.
                for (const auto &schedule : fused_op_info.schedules) {
                    int ublock_mutliplier = 1;
                    int matmul_0_intermed_index = -1;
                    const auto &last_op = schedule.scheduled_ops.back();
                    bool schedule_ends_with_matmul = netlist_utils::is_valid_matmul_op(last_op.type);
                    bool non_column_input1 = last_op.output_dim.mblock_n > 1;
                    bool intermed_buff_input0 = last_op.input_names[0].find("intermed") != std::string::npos;
                    if (schedule_ends_with_matmul && non_column_input1 && intermed_buff_input0) {
                        // This is a schedule which ends with matmul and input0 of this matmul is an intermed buffer
                        // and input1 is non-column matrix.
                        // In this case we need to extend the size of intermed buffer which is the input0 of this matmul
                        // to hold m_k micro blocks of input0.
                        matmul_0_intermed_index = atoi(&last_op.input_names[0].back());
                        ublock_mutliplier = last_op.m_k;
                    }

                    for (auto &scheduled_op : schedule.scheduled_ops) {
                        if (scheduled_op.output.find("intermed") != std::string::npos) {
                            int intermed_index = atoi(&scheduled_op.output.back());
                            int ublock_size = scheduled_op.output_dim.ublock_ct * scheduled_op.output_dim.ublock_rt;
                            int tile_size_requirement = ublock_size;
                            if (!netlist_utils::is_valid_matmul_op(scheduled_op.type) && matmul_0_intermed_index == intermed_index) {
                                tile_size_requirement *= ublock_mutliplier;
                            }
                            fused_op_info.intermed_buff_size_in_tiles[intermed_index] = std::max(
                                fused_op_info.intermed_buff_size_in_tiles[intermed_index], tile_size_requirement);
                        }
                    }
                }

                fused_ops_op_map.insert({op_it.first + "_" + fused_op_id, fused_op_info});

                // Copy the fused_op_info to the base op_info
                op_it.second.fused_op_info = fused_op_info;
            } else if (op_it.second.type == "splice") {
                bool keep_specified_for_all_inputs = op_it.second.input_dims.size() == op_it.second.attributes.splice_infos.size();
                log_assert(keep_specified_for_all_inputs,
                    "splice op {} must have input_dims.size()={} equal to splice_infos.size()={}",
                    op_it.second.name,
                    op_it.second.input_dims.size(),
                    op_it.second.attributes.splice_infos.size());
                for (int input_index = 0; input_index < op_it.second.input_dims.size(); input_index++) {
                    validation::validate_splice_op_attributes(op_it.second, input_index);
                }
            }
        }
    }
}

void netlist_parser::derive_input_tile_dims_for_ops() {
    for (auto &graph_it : this->graph_map) {
        for (auto &op_it : graph_it.second.op_map) {
            tt_op_info &op_info = op_it.second;
            for (int i = 0; i < op_info.input_names.size(); i++) {
                TileDim tile_dim = TileDim::Default;

                const string& input_name = op_info.input_names[i];

                if(graph_it.second.op_map.find(input_name) != graph_it.second.op_map.end()) {
                    tile_dim = graph_it.second.op_map.at(input_name).output_tile_dim;
                } else if (queue_map.find(input_name) != queue_map.end()) {
                    tile_dim = queue_map.at(input_name).tile_dim;
                }

                // Take into account potential transpose tm
                if (op_info.input_tm_ops.find(i) != op_info.input_tm_ops.end()) {
                    for (int j = 0; j < op_info.input_tm_ops.at(i).size(); j++) {
                        TmOp tm_op = netlist_utils::get_tm_op(op_info.input_tm_ops.at(i).at(j).first);
                        if (tm_op == TmOp::Transpose) {
                            if (netlist_utils::is_valid_matmul_op(op_info.type)) {
                                log_assert(
                                    tile_dim == TileDim::Dim32x32 || tile_dim == TileDim::Dim16x32,
                                    "Transpose TM is supported on matmul input only if it's tile dims are 32x32 or "
                                    "16x32. Op name: {}, input tile dim: {}",
                                    op_info.name,
                                    get_string(tile_dim));
                            }
                            tile_dim = transpose_tile_dim(tile_dim);
                        }
                    }
                }
                op_info.input_tile_dims.push_back(tile_dim);
            }
        }
    }
}

void netlist_parser::derive_vector_mode_for_ops() {
    for (auto &graph_it : this->graph_map) {
        for (auto &op_it : graph_it.second.op_map) {
            tt_op_info &op_info = op_it.second;

            // If the user has specifically set vector mode, respect that choice
            if (op_info.attributes.vector_mode != Dim::Invalid) {
                continue;
            }

            // Otherwise, we set vector mode to RC, except if we have sfpu ops with tiny tiles.
            // In those cases, we pick the proper value based on tile dims.
            // 32x32 -> Dim::RC
            // 32x16 -> Dim::C
            // 1x32, 2x32, 4x32, 8x32, 16x32 -> Dim::R
            if (netlist_utils::is_valid_sfpu_op(op_info.type)) {
                TileDim input_tile_dims = op_info.input_tile_dims[0];
                if (input_tile_dims == TileDim::Dim32x32) {
                    op_info.attributes.vector_mode = Dim::RC;
                } else if (input_tile_dims == TileDim::Dim32x16) {
                    op_info.attributes.vector_mode = Dim::C;
                } else {
                    op_info.attributes.vector_mode = Dim::R;
                }
            } else {
                op_info.attributes.vector_mode = Dim::RC;
            }
        }
    }
}

void netlist_parser::derive_input_dims_for_ops() {
    // op name to list of producers
    std::unordered_map<std::string, std::pair<tt_dim_info, tt::tt_grid_shape>> op_dims;

    for (auto &graph_it : this->graph_map) {
        for (auto &op_it : graph_it.second.op_map) {
            op_dims.insert(std::make_pair(op_it.first, std::make_pair(op_it.second.output_dim, op_it.second.grid_size)));
        }
    }

    for (auto &graph_it : this->graph_map) {
        for (auto &op_it : graph_it.second.op_map) {
            derive_input_dims_for_op(op_it.second, op_dims);
        }
    }
}

void netlist_parser::derive_input_dims_for_op(
    tt_op_info &op_info,
    const std::unordered_map<std::string, std::pair<tt_dim_info, tt::tt_grid_shape>>& op_dims) {
    int input_index = 0;
    for (const auto &input_name : op_info.input_names) {
        bool input_found = false;
        if (op_info.is_input_queue[input_index++]) {
            // found producing queue
            op_info.input_dims.push_back(queue_map.at(input_name).dim);
            op_info.input_core_grids.push_back(queue_map.at(input_name).grid_size);
            input_found = true;
        } else {
            // find producing op
            auto op_it = op_dims.find(input_name);
            if (op_it != op_dims.end()) {
                op_info.input_dims.push_back(op_it->second.first);
                op_info.input_core_grids.push_back(op_it->second.second);
                input_found = true;
            }
        }
        log_assert(
            input_found,
            "Trying to derive input_dims for op={} but cannot find producer for input={}",
            op_info.name,
            input_name);
    }

    log_assert(
        op_info.input_names.size() == op_info.input_dims.size(),
        "op={} input_names.size()={} must be equal to input_dims.size()={} which are derived",
        op_info.name,
        op_info.input_names.size(),
        op_info.input_dims.size());
}
bool netlist_parser::is_queue_fed_by_op(tt_queue_info queue) {
    return !(queue.input == "HOST" or queue.input.find("net2net") != string::npos);
}

void netlist_parser::expand_multi_instance_structures() {
    // Expand each multi-device queue into multiple single-device queues
    for (auto &queue : queue_to_devices_map) {
        for (auto &device : queue.second) {
            tt_queue_info device_queue = queue_map[queue.first];
            device_queue.target_device = device;
            if (device_queue.input != "HOST")
                device_queue.input = device_queue.input + "." + std::to_string(device);
            std::string name = queue.first + "." + std::to_string(device);
            device_queue.name = name;
            queue_map[name] = device_queue;
        }
        queue_map.erase(queue.first);
    }
    // Expand each multi-device graph into multiple single-device graphs
    for (auto &graph : graph_to_devices_map) {
        for (auto &device : graph.second) {
            tt_graph_info device_graph = graph_map[graph.first];
            device_graph.target_device = device;
            device_graph.name = device_graph.name + "." + std::to_string(device);

            std::map<string, tt_op_info> &op_map = device_graph.op_map;
            for (auto &op : op_map) {
                op.second.name = op.second.name + "." + std::to_string(device);
                for (auto &input_name : op.second.input_names) {
                    input_name = input_name + "." + std::to_string(device);
                }
            }
            std::map<string, tt_op_info> op_map_;
            for (auto &op : op_map) {
                op_map_[op.first + "." + std::to_string(device)] = op.second;
            }
            device_graph.op_map = op_map_;

            std::string name = graph.first + "." + std::to_string(device);
            graph_map[name] = device_graph;
        }
        graph_map.erase(graph.first);
    }
    // Expand each multi-device graph in graph_order
    if (graph_to_devices_map.size()) {
        std::vector<std::string> graph_order_ = graph_order;
        graph_order.clear();
        for (auto &graph : graph_order_) {
            if (graph_to_devices_map.find(graph) != graph_to_devices_map.end()) {
                for (auto &device : graph_to_devices_map[graph]) {
                    string name = graph + "." + std::to_string(device);
                    graph_order.push_back(name);
                }
            }
        }
    }
    // Expand each multi-device instruction in program_trace
    if (queue_to_devices_map.size() or graph_to_devices_map.size()) {
        for (auto &program : program_map) {
            std::vector<tt_instruction_info> program_trace_ = program.second.program_trace;
            program.second.program_trace.clear();
            for (auto &instrn : program_trace_) {
                if (graph_to_devices_map.find(instrn.graph_name) != graph_to_devices_map.end()) {
                    for (auto &device : graph_to_devices_map[instrn.graph_name]) {
                        tt_instruction_info instrn_ = instrn;
                        instrn_.graph_name = instrn_.graph_name + "." + std::to_string(device);
                        for (auto &queue_setting : instrn_.queue_settings) {
                            queue_setting.name = queue_setting.name + "." + std::to_string(device);
                        }
                        program.second.program_trace.push_back(instrn_);
                    }
                } else {
                    program.second.program_trace.push_back(instrn);
                }
            }
        }
    }
}

void netlist_parser::verify_ublock_fits_into_dest(const string& op_name, const tt_dim_info& op_output_dim, DataFormat op_dest_accumulate_data_format, bool full_sync_mode, tt::ARCH arch) {
    std::uint32_t max_ublock_dim = full_sync_mode ? DstSize::FullSize : DstSize::HalfSize;
    if ((op_dest_accumulate_data_format == DataFormat::Float32) or 
        (op_dest_accumulate_data_format == DataFormat::Int32)) {
        max_ublock_dim /= 2; // default dst size in tiles is for Float16 format
    }

    if (arch == tt::ARCH::WORMHOLE) {
        max_ublock_dim *= 4; // scaling up to accomodate 4x larger dst-size for a0
    }

    const std::uint32_t ublock_dim = op_output_dim.ublock_rt * op_output_dim.ublock_ct;

    log_assert(
        ublock_dim <= max_ublock_dim,
        "Op={} ublock dim rt={},ct={} can't fit into dest! Max ublock_dim(rt*ct)={}",
        op_name,
        op_output_dim.ublock_rt,
        op_output_dim.ublock_ct,
        max_ublock_dim);
}

void netlist_parser::verify_complex_settings() {
    std::vector<std::tuple<std::string, std::string, TileDim>> queue_name_and_producer_name_and_tile_dim;

    for (const auto &queue_it : queue_map) {
        string queue_name = queue_it.second.name;
        if(is_queue_fed_by_op(queue_it.second)) {
            string producer_name = queue_it.second.input;
            queue_name_and_producer_name_and_tile_dim.emplace_back(std::make_tuple(queue_name, producer_name, queue_it.second.tile_dim));
        }
    }

    // Validate 1 producer for each each Queue if it is output of graph
    // Verify output queue tile dims
    tt::parallel_for(queue_name_and_producer_name_and_tile_dim.begin(), queue_name_and_producer_name_and_tile_dim.end(), [&](const auto &tuple) {
        int producer_count = 0;
        for (const auto &graph_it : graph_map) {
            for (const auto &op_it : graph_it.second.op_map) {
                if (op_it.first == std::get<1>(tuple)) {
                    producer_count++;
                }

                if (op_it.second.name == std::get<1>(tuple)) {
                    log_assert(std::get<2>(tuple) == op_it.second.output_tile_dim, "Op and the queue it's feeding must have the same tile dim!");
                }
            }
        }
        log_assert(
            producer_count != 0,
            "Cannot find the producer op for queue = {} Producer Op = {}",
            std::get<0>(tuple),
            std::get<1>(tuple));
        log_assert(
            producer_count == 1,
            "Producer count of queues that are outputted by device must be 1.  I.E only 1 op can produce the "
            "output for queue = {}",
            std::get<0>(tuple));
    }, tt::cpuset::get_allowed_num_threads());

    // Check the consumer -- This should always be used if it exists
    for (const auto &graph_it : graph_map) {  // Check the consumer
        for (const auto &op_it : graph_it.second.op_map) {
            if (op_it.second.has_queue_inputs) {
                for (int i=0; i < op_it.second.input_names.size(); i++) {
                    if (op_it.second.is_input_queue[i]) {
                        tt_queue_info& queue_info = queue_map[op_it.second.input_names[i]];
                        if (queue_info.type == IO_TYPE::Queue) {
                            log_assert(
                                graph_it.second.input_count == queue_info.input_count,
                                "Input count derived for queue={} must match graph which consumes input_count",
                                op_it.second.input_names[i]);
                        } else {
                            bool param_or_constant = queue_info.entries == 1;
                            if (param_or_constant) {
                                log_assert(queue_info.input_count == 1, "Input count for param/constant ram must be 1");
                            }
                        }
                    }
                }
            }
        }
    }

    // Verify every op name is unique
    std::unordered_set<std::string> op_names;
    std::vector<std::string> repeated_op_names;
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            if (op_names.find(op_it.first) != op_names.end()) {
                repeated_op_names.push_back(op_it.first);
            } else {
                op_names.insert(op_it.first);
            }
        }
    }
    for (const auto &op_name : repeated_op_names) {
        log_error("Op {} is not globally unique", op_name);
    }
    log_assert(repeated_op_names.size() == 0, "All Op names must be globally unique across graphs");

    // Verify tile dims for each op.
    for (const auto &graph_it : graph_map) {
        for (const auto& op_it : graph_it.second.op_map) {
            const tt_op_info& op_info = op_it.second;

            bool is_nop = (is_valid_unary_op(op_info.type) && (get_valid_unary_op(op_info.type) == UnaryOp::Datacopy));
            bool is_tilizer = is_valid_tilizer_op(op_info.type);
            bool is_sfpu_op = is_valid_sfpu_op(op_info.type);
            bool is_binary_op = is_valid_binary_op(op_info.type);
            bool is_unary_or_binary = is_nop || is_binary_op || is_sfpu_op || is_tilizer;
            bool is_matmul = netlist_utils::is_valid_matmul_op(op_info.type);
            bool is_dense_matmul = is_matmul && !op_info.attributes.identity;
            bool is_identity_matmul = is_matmul && op_info.attributes.identity;
            
            // Input tile dims verification
            for (int i = 0; i < op_info.input_tile_dims.size(); i++) {
                TileDim input_tile_dim = op_info.input_tile_dims[i];

                switch (input_tile_dim) {
                    case TileDim::Dim1x32: {
                        log_assert(
                            is_dense_matmul || (is_identity_matmul && (i == 1)) || is_unary_or_binary,
                            "TileDim 1x32 is supported on dense matmul inputs or on identity matmul input1 or"
                            "on datacopy, sfpu, or binary ops. Tried to use it on op: name {} type {} input index {}. ",
                            op_info.name,
                            op_info.type,
                            i);
                        break;
                    }
                    case TileDim::Dim2x32:
                    case TileDim::Dim4x32:
                    case TileDim::Dim8x32:
                    case TileDim::Dim16x32: 
                    case TileDim::Dim32x16:{
                        log_assert(
                            (is_dense_matmul && (i != 2)) || (is_identity_matmul && (i == 1)) || is_unary_or_binary,
                            "TileDims 2x32, 4x32, 8x32, 16x32 and 32x16 are supported on dense matmul on input0 or "
                            "input1, on identity matmul input1 or on datacopy, sfpu or binary ops. Tried to use it on "
                            "op: name {} type {} input index {}.",
                            op_info.name,
                            op_info.type,
                            i);
                        break;
                    }
                    case TileDim::Dim32x32:
                        // Supported on all inputs on all ops
                        break;
                    default:
                        log_error(
                            "Unsupported tile dim: {} on op: name {}, type {} input index{}.",
                            input_tile_dim,
                            op_info.name,
                            op_info.type,
                            i);
                        break;
                }
            }

            // Tile dims and dataformat match:
            // Tile size must be 32B aligned in order for NOC to work.
            // This is currently not happening if:
            // 1. We have bfp2 (a and b) with tile_dims 1x32 and 2x32
            // 2. We have bfp4 (a and b) with tile_dims 1x32.
            {
                std::vector<int> tile_dim_array = tile_dim_to_array(op_info.output_tile_dim);
                int tile_size = size::get_tile_size_in_bytes(
                    op_info.output_data_format, true, tile_dim_array[0], tile_dim_array[1]);
                log_assert(
                    tile_size % 32 == 0,
                    "Op: {} output data format {} and tile dim {} combination is not supported because it is not 32B "
                    "aligned. "
                    "Tile size in this case is {}B.",
                    op_info.name,
                    op_info.output_data_format,
                    get_string(op_info.output_tile_dim),
                    tile_size);

                // Verify inputs
                for (int i = 0; i < op_info.input_tile_dims.size(); i++) {
                    tile_dim_array = tile_dim_to_array(op_info.input_tile_dims[i]);
                    tile_size = size::get_tile_size_in_bytes(
                        op_info.input_data_formats[i], true, tile_dim_array[0], tile_dim_array[1]);
                    log_assert(
                        tile_size % 32 == 0,
                        "Op: {} input {} data format {} and tile dim {} combination is not supported because it is not "
                        "32B aligned. Tile size in this case is {}B.",
                        op_info.name,
                        i,
                        op_info.input_data_formats[i],
                        get_string(op_info.input_tile_dims[i]),
                        tile_size);
                }
            }

            // Int8, Int32 data formats are not supported with tiny tiles.
            {
                bool has_tiny_tile = false;
                for (int i = 0; i < op_info.input_tile_dims.size(); i++) {
                    TileDim inp_tile_dim = op_info.input_tile_dims[i];
                    if (inp_tile_dim != TileDim::Dim32x32) {
                        has_tiny_tile = true;
                        break;
                    }
                }
                has_tiny_tile = has_tiny_tile || op_info.output_tile_dim != TileDim::Dim32x32;
                if (has_tiny_tile) {
                    for (int i = 0; i < op_info.input_data_formats.size(); i++) {
                        DataFormat in_df = op_info.input_data_formats[i];
                        log_assert(
                            in_df != DataFormat::Int8 && in_df != DataFormat::Int32,
                            "Tiny tiles with Int8 and Int32 input formats are not supported. Op name: {}",
                            op_info.name);
                    }
                    log_assert(
                        op_info.output_data_format != DataFormat::Int8 &&
                            op_info.output_data_format != DataFormat::Int32,
                        "Tiny tiles with Int8 and Int32 output formats are not supported. Op name: {}",
                        op_info.name);
                }
            }

            // On sfpu ops, if input is != Nx32 (where N != 32), vector mode must be r.
            // If input is 32x16, vector mode must be c.
            if (is_sfpu_op) {
                if (op_info.input_tile_dims[0] != TileDim::Dim32x32 &&
                    op_info.input_tile_dims[0] != TileDim::Dim32x16) {
                    log_assert(
                        op_info.attributes.vector_mode == Dim::R,
                        "If sfpu op has 1x32, 2x32, 4x32, 8x32 or 16x32 input tile dims, then vector mode attribute "
                        "must be set to R. Op "
                        "{} "
                        "Vector mode: {}",
                        op_info.name,
                        get_string(op_info.attributes.vector_mode));
                }
                if (op_info.input_tile_dims[0] == TileDim::Dim32x16) {
                    log_assert(
                        op_info.attributes.vector_mode == Dim::C,
                        "If sfpu op has 32x16 input tile dims, then vector mode attribute must be set to C. Op "
                        "{} Vector mode: {}",
                        op_info.name,
                        get_string(op_info.attributes.vector_mode));
                }
            }

            // All tile dims on binary op inputs must be the same
            if (is_binary_op) {
                log_assert(
                    op_info.input_tile_dims[0] == op_info.input_tile_dims[1],
                    "All inputs on binary ops (add, multiply, subtract) must have the same tile dims! Op {} does "
                    "not have the same tile sizes on all inputs.",
                    op_info.name);
            }

            // Output tile dims verification
            if (is_unary_or_binary) {
                // If input is 32x16, output must be 32x16
                TileDim input_tile_dim = op_info.input_tile_dims[0];
                TileDim output_tile_dim = op_info.output_tile_dim;
                if (input_tile_dim == TileDim::Dim32x16) {
                    log_assert(
                        input_tile_dim == output_tile_dim,
                        "If sfpu, binary or datacopy op has 32x16 input tile dims, output must have 32x16 tile dims as well. Op: {} Output "
                        "tile dims: {}",
                        op_info.name,
                        get_string(output_tile_dim));
                }

                // If output is 32x16, input must be 32x16
                if (output_tile_dim == TileDim::Dim32x16) {
                    log_assert(
                        input_tile_dim == output_tile_dim,
                        "If spfu, binary or datacopy op has 32x16 output tile dims, input must have 32x16 tile dims as well. Op: {} Input "
                        "tile dims: {}",
                        op_info.name,
                        get_string(input_tile_dim));
                }
            }

            // If we have datacopy op, with gradient_op == true, then input tile dims must be equal to output tile dims.
            // #Issue 2126
            if (is_nop && op_info.gradient_op) {
                log_assert(
                    op_info.input_tile_dims[0] == op_info.output_tile_dim,
                    "On datacopy op, with gradient_op == true, input tile dims must be equal to output tile dims. "
                    "#Issue 2126. Op {} Input tile dims: {} Output tile dims: {}",
                    op_info.name,
                    get_string(op_info.input_tile_dims[0]),
                    get_string(op_info.output_tile_dim));
            };

            if (is_dense_matmul) {
                bool tiny_tiles_enabled = false;
                for (int i = 0; i < 2; i++) {
                    TileDim input_dim = op_info.input_tile_dims[i];
                    if (input_dim != TileDim::Dim32x32) {
                        tiny_tiles_enabled = true;
                        break;
                    }
                }
                tiny_tiles_enabled |= (op_info.output_tile_dim != TileDim::Dim32x32);

                // Fusing matmul + sfpu op is not supported (except the case where bias is 1x32, in that case it is allowed). #Issue 2174
                if (op_info.attributes.sfpu_op != SfpuOp::Invalid) {
                    log_assert(
                        tiny_tiles_enabled == false,
                        "Fusing matmul with sfpu ops is not supported when using tiny tiles feature. #Issue 2174. Op "
                        "name: {}",
                        op_info.name);
                }

                // Matmul with gradient op is not supported with tiny tiles.
                if (op_info.gradient_op) {
                    log_assert(
                        tiny_tiles_enabled == false,
                        "Matmul with gradient accomulation is not supported when using tiny tiles feature. Op name: {}",
                        op_info.name);
                }
            }

            if (is_matmul) {
                TileDim input_1_tile_dims = op_info.input_tile_dims[1];
                TileDim output_tile_dim = op_info.output_tile_dim;

                if (input_1_tile_dims == TileDim::Dim32x16) {
                    log_assert(
                        output_tile_dim == TileDim::Dim32x16,
                        "If matmul input1 has 32x16 tile dims, output must be 32x16. Op name: {} "
                        "Output tile dims: {}",
                        op_info.name,
                        get_string(output_tile_dim));
                }

                if (output_tile_dim == TileDim::Dim32x16) {
                    log_assert(
                        input_1_tile_dims == TileDim::Dim32x16,
                        "If matmul output tile dims are 32x16, input 1 tile dims must be 32x16. "
                        "Op name {} Input 1 tile dims: {}",
                        op_info.name,
                        get_string(input_1_tile_dims));
                }
            }

            // For reduce r max, we allow Nx32 output tile dims
            bool is_reduce_r_max = netlist_utils::is_valid_reduce_op(op_info.type) &&
                                   op_info.attributes.reduce_type == ReduceFunc::Max &&
                                   op_info.attributes.reduce_dim == Dim::R;
            if (is_reduce_r_max) {
                log_assert(
                    op_info.output_tile_dim != TileDim::Dim32x16,
                    "Output tile dim 32x16 is invalid for reduce row, valid tile dims are Nx32. Op name: {}",
                    op_info.name);
            }

            // For reduce c max, we allow 32x16 and 32x32 output tile dims
            bool is_reduce_c_max = netlist_utils::is_valid_reduce_op(op_info.type) &&
                                   op_info.attributes.reduce_type == ReduceFunc::Max &&
                                   op_info.attributes.reduce_dim == Dim::C;
            if (is_reduce_c_max) {
                log_assert(
                    op_info.output_tile_dim == TileDim::Dim32x32 || op_info.output_tile_dim == TileDim::Dim32x16,
                    "Output tile dims 32x32 and 32x16 are allowed for reduce column. Op name: {} output tile dims: {}",
                    op_info.name,
                    get_string(op_info.output_tile_dim));
            }
        }
    }
    
    // START: Untilized output queue rules
    // Grid size must be 1,1
    // Must feed only 1 output queue and not an op
    // Queue cannot be used by any other ops as input

    // Find all untilize_output_ops
    std::set<std::string> untilize_output_ops = {};
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            tt_op_info op_info = op_it.second;
            if (op_it.second.untilize_output) {
                untilize_output_ops.insert(op_info.name);
            }
        }
    }

    // Verify that ublock can fit into dest
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            const tt_op_info& op_info = op_it.second;
            
            verify_ublock_fits_into_dest(
                op_info.name,
                op_info.output_dim,
                op_info.dest_accumulate_data_format,
                false,
                device_info.arch);
            
            if (is_valid_fused_op(op_info.type)) {
                for (const auto& schedule: op_info.fused_op_info.schedules) {
                    for (const auto& scheduled_op: schedule.scheduled_ops) {
                        verify_ublock_fits_into_dest(
                            scheduled_op.name,
                            scheduled_op.output_dim,
                            op_info.dest_accumulate_data_format,
                            /*full sync mode*/ false,
                            device_info.arch
                        );
                    }
                }
            }

        }
    }

    // Find all untilize_output_queues + check sizing
    std::set<std::string> untilize_output_queues = {};
    for (const auto &untilize_op : untilize_output_ops) {
        bool found = false;
        std::string found_queue = "";
        for (const auto &queue_it : queue_map) {
            if (untilize_op == queue_it.second.input) {
                log_assert(
                    (queue_it.second.grid_size.r == 1) and (queue_it.second.grid_size.c == 1),
                    "Grid size must be [1,1] for queue={} that are fed by untilize_output ops",
                    queue_it.second.name);
                log_assert(
                    not found,
                    "Op={} with untilize output must only feed only 1 queue -- Forking is not supported. Used to feed "
                    "queues={},{}",
                    untilize_op,
                    queue_it.second.name,
                    found_queue);
                found_queue = queue_it.second.name;
                untilize_output_queues.insert(queue_it.second.name);
                found = true;
            }
        }
        log_assert(found, "Untilized ops must feed a queue");
    }

    // Traverse all op inputs and make sure none of them are fed by untilized output ops or queues
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            tt_op_info op_info = op_it.second;
            for (const auto &input : op_it.second.input_names) {
                if (untilize_output_ops.find(input) != untilize_output_ops.end()) {
                    log_fatal(
                        "Input={} to op={} is an op with untilize output.  Untilized output is not allowed to feed "
                        "other ops",
                        input,
                        op_it.second.name);
                } else if (untilize_output_queues.find(input) != untilize_output_queues.end()) {
                    log_fatal(
                        "Input={} to op={} is an IO which is written to by an untilized output op.  Untilized output "
                        "is not allowed to feed other ops",
                        input,
                        op_it.second.name);
                }
            }
        }
    }

    // Related Check: Traverse all queues, and if location is HOST, ensure input is an untilized output op.
    // for (const auto &queue_it : queue_map) {
    //     if (queue_it.second.loc == QUEUE_LOCATION::HOST) {
    //         log_assert(
    //             untilize_output_ops.find(queue_it.second.input) != untilize_output_ops.end(),
    //             "Host queue={} must have input that is op with untilized output.",
    //             queue_it.second.name);
    //     }
    // }

    // END

    // All graphs must be assigned to temporal graphs
    for (const auto &graph_it : graph_map) {
        if (this->graph_temporal_graphs.find(graph_it.first) == this->graph_temporal_graphs.end()) {
            log_fatal("Graph {} was not assigned to a temporal graph", graph_it.first);
        }
        const temporal_graph_id_t temporal_graph = this->graph_temporal_graphs.at(graph_it.first);
        if (this->temporal_graph_graphs.at(temporal_graph).find(graph_it.first) ==
            this->temporal_graph_graphs.at(temporal_graph).end()) {
            log_fatal(
                "Graph {} was mapped to temporal graph {} but temporal graph {} does not list this graph as belonging "
                "to it",
                graph_it.first,
                temporal_graph,
                temporal_graph);
        }
    }

    std::unordered_map<std::string, std::string> op_graph_map = {};
    if (is_queueless_multichip_supported(device_info.arch)) {
        for (const auto &[graph_name, graph] : graph_map) {
            for (const auto &[op_name, op] : graph.op_map) {
                op_graph_map.insert({op_name, graph_name});
            }
        }
    }

    // Inputs to Ops either have to come from a queue, or an op in the current graph
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            int input_index = 0;
            for (const auto &input : op_it.second.input_names) {
                bool input_is_queue = op_it.second.is_input_queue[input_index++];
                bool input_is_op_in_graph = (graph_it.second.op_map.find(input) != graph_it.second.op_map.end());
                if ((not input_is_op_in_graph) and (not input_is_queue)) {
                    if (is_queueless_multichip_supported(device_info.arch)) {
                        auto my_owner_temporal_graph = this->graph_temporal_graphs.at(graph_it.first);
                        const auto &input_op_graph = op_graph_map.at(input);
                        auto my_input_owner_temporal_graph = this->graph_temporal_graphs.at(input_op_graph);
                        if (my_owner_temporal_graph != my_input_owner_temporal_graph) {
                            log_fatal(
                                "input={} to op={} must belong to the same temporal graph. graph: {}, temporal_graph: "
                                "{}. input op graph {}, input op temporal_graph: {}"
                                "communication must be through queues/buffers",
                                input,
                                op_it.second.name,
                                graph_it.first,
                                my_owner_temporal_graph,
                                input_op_graph,
                                my_input_owner_temporal_graph);
                        }
                    } else {
                        log_fatal(
                            "input={} to op={} must be either a queue, or an op within the graph, cross graph "
                            "communication must be through queues/buffers",
                            input,
                            op_it.second.name);
                    }
                }
            }
        }
    }

    // Verify that only rams must set wr_ptr_global and rd_ptr_global.
    //                  queues must never set wr_ptr_global.
    for (const auto &program_it : program_map) {
        for (const auto &instruction : program_it.second.program_trace) {
            for (const auto &queue_setting : instruction.queue_settings) {
                if (queue_map.find(queue_setting.name) == queue_map.end()) {
                    log_fatal(
                        "queue={} in program={} queue_settings set but not declared in queue_map section",
                        queue_setting.name,
                        program_it.first);
                } else {
                    const auto& queue = queue_map.at(queue_setting.name);
                    if (queue.type == IO_TYPE::Queue) {
                        if (queue_setting.wr_ptr_global != "") {
                            log_fatal(
                                "queue={} in program={} queue_settings set wr_ptr_global, but wr_ptr_global should "
                                "only be set for Random Access types",
                                queue_setting.name,
                                program_it.first);
                        }
                        if ((queue_setting.rd_ptr_global == "") or (queue_setting.rd_ptr_local == "")) {
                            log_fatal(
                                "queue={} in program={} queue_settings must set rd_ptr_global and rd_ptr_local for "
                                "Queue types",
                                queue_setting.name,
                                program_it.first);
                        }
                        // A dram queue that is output of an OP and has epilogue=true is not supported.
                        // Gitlab issue: budabackend#1818
                        if (queue_setting.epilog == "true" && is_queue_fed_by_op(queue)) {
                           log_fatal(
                               "The queue_settings for queue={} in program={} must not set 'epilogue: true'. "
                               "It is not supported for a dram queue of 'type: queue' "
                               "that is output of an operation.",
                               queue_setting.name,
                               program_it.first);
                        }
                    } else if (queue.type == IO_TYPE::RandomAccess) {
                        if (queue_setting.rd_ptr_local != "") {
                            log_fatal(
                                "queue={} in program={} queue_settings set rd_ptr_local, but rd_ptr_local should only "
                                "be set for Queue types",
                                queue_setting.name,
                                program_it.first);
                        }
                        if ((queue_setting.rd_ptr_global == "") or (queue_setting.wr_ptr_global == "")) {
                            log_fatal(
                                "queue={} in program={} queue_settings must set rd_ptr_global and wr_ptr_global for "
                                "Random Access types",
                                queue_setting.name,
                                program_it.first);
                        }
                    } else {
                        log_fatal(
                            "Invalid Queue Type detected at final validation for queue={}",
                            queue_setting.name);
                    }
                    if (queue_setting.global_rdptr_autoinc != "0" and queue_setting.rd_ptr_autoinc != "0") {
                        if (queue_setting.rd_ptr_autoinc != queue_setting.global_rdptr_autoinc) {
                            log_fatal(
                                "queue={} in program={} queue_settings set rd_ptr_autoinc={} and global_rdptr_autoinc={}, but "
                                "local and global autoinc must be the same when global_rdptr_autoinc is set",
                                queue_setting.name,
                                program_it.first,
                                queue_setting.rd_ptr_autoinc,
                                queue_setting.global_rdptr_autoinc);
                        }
                    }
                }
            }
        }
    }
    // Verify that if params are specified, they must be first instruction, and only 1
    for (const auto &program_it : program_map) {
        for (uint32_t idx = 0; idx < program_it.second.program_trace.size(); idx++) {
            const auto &instruction = program_it.second.program_trace.at(idx);
            if (instruction.opcode == INSTRUCTION_OPCODE::Param) {
                if (idx != 0) {
                    log_fatal(
                        "Param Instruction must be first instruction in program={} if it exists",
                        program_it.second.name);
                }
            }
        }
    }

    // Verify op configs based on hardware constraints
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            tt_op_info op_info = op_it.second;

            if (device_info.arch == tt::ARCH::GRAYSKULL) {
                log_assert(
                    (op_info.dest_accumulate_data_format != DataFormat::Float32),
                    "Grayskull does not support dest in fp32 mode");
                log_assert(
                    (op_info.dest_accumulate_data_format != DataFormat::Int32),
                    "Grayskull does not support dest in int32 mode");

                if ((op_info.output_data_format != op_info.intermed_data_format) &&
                    (op_info.gradient_op || (op_info.attributes.m_k > 1))) {
                    log_assert(
                        (is_exp_a_format(op_info.output_data_format) && is_exp_a_format(op_info.intermed_data_format)) |
                            (is_exp_b_format(op_info.output_data_format) &&
                             is_exp_b_format(op_info.intermed_data_format)) |
                            ((op_info.output_data_format == DataFormat::Float32) &&
                             is_exp_b_format(op_info.intermed_data_format)),
                        "grayskull constraint: output_df and intermediate_df must have same exponent width for "
                        "reconfiguring data format");
                }

                log_assert(
                    (op_info.attributes.sfpu_execution_thread == SfpuExecutionThread::Math),
                    "Grayskull does not support SFPU execution on non-math threads"
                );
            }

            if (op_info.gradient_op ||
                (is_valid_matmul_op(op_info.type) && (op_info.attributes.m_k > 1 || op_info.attributes.bias))) {
                // Intermediates cannot be bfp4/2 due to issue:
                // https://yyz-tensix-gitlab.local.tenstorrent.com/tenstorrent/tensix/-/issues/9163 which causes values
                // to saturate -inf
                log_assert(
                    !is_bfp2_format(op_info.intermed_data_format) && !is_bfp4_format(op_info.intermed_data_format),
                    "Intermediates cannot be bfp4/2 for accumlations due to -inf saturation");
            }

            if (op_info.dest_accumulate_data_format == DataFormat::Float32) {

                if(is_valid_matmul_op(op_info.type)) {
                    bool accumulate_en = (device_info.arch == tt::ARCH::WORMHOLE_B0) ? (op_info.attributes.l1_acc) : (op_info.attributes.m_k > 1);
    
                    if(accumulate_en) {
                        log_assert(
                        (op_info.intermed_data_format == op_info.dest_accumulate_data_format),
                        "For matmul accumulations and accumulate data format Fp32, intermediate data format must be Fp32");
                    }
                }

                // Constraint: When fp32 dest accumulation is enabled for wh_a0, and the op will use a2d, we have to set
                // in_0 or in_1 to fp32 depending on the op type.
                // If gradient op it uses ELWADD
                if ((device_info.arch == tt::ARCH::WORMHOLE) and !op_info.gradient_op and (is_valid_sfpu_op(op_info.type) || get_unary_op(op_info.type) == UnaryOp::Datacopy)) {
                    log_assert(
                        op_info.input_data_formats.at(0) == DataFormat::Float32,
                        "wh_a0 hardware constraint: sfpu op is using a2d op with fp32 dest acc enabled. Must set "
                        "in0_df to Float32");
                }
                
                //Issue: #1816
                log_assert(op_info.attributes.stoch_rnd_mode != StochRndMode::Fpu && op_info.attributes.stoch_rnd_mode != StochRndMode::All,
                "HW constraint: stochastic fpu rounding cannot be enabled with float32 dest");
            }

            if (op_info.dest_accumulate_data_format == DataFormat::Int32) {

                if(is_valid_matmul_op(op_info.type)) {
                    bool accumulate_en = (device_info.arch == tt::ARCH::WORMHOLE_B0) ? (op_info.attributes.l1_acc) : (op_info.attributes.m_k > 1);
    
                    if(accumulate_en) {
                        log_assert(
                        (op_info.intermed_data_format == op_info.dest_accumulate_data_format),
                        "For matmul accumulations and accumulate data format Int32, intermediate data format must be Int32");
                    }
                }

                log_assert(op_info.gradient_op == false, "gradient accumulation for int32 dest is not supported");
            }

            // For gradient ops, output df must be the same as intermed df
            if (op_info.gradient_op) {
                log_assert(
                    op_info.intermed_data_format == op_info.output_data_format,
                    "intermediate and output data format must be the same for gradient op");
                // Issue to not support transpose+gradients
                // https://yyz-gitlab.local.tenstorrent.com/tenstorrent/pybuda/-/issues/374
                log_assert(
                    (not op_info.transpose) or (netlist_utils::is_valid_matmul_op(op_info.type) or
                                                (netlist_utils::get_unary_op(op_info.type) == UnaryOp::Datacopy)),
                    "gradient ops does not support input transpose except for matmul and datacopy");

                if ((device_info.arch != tt::ARCH::WORMHOLE_B0) && op_info.dest_accumulate_data_format == DataFormat::Float32) {
                    log_assert(op_info.intermed_data_format == op_info.dest_accumulate_data_format,
                    "GS/WHA0 constraint: For gradients and accumulate data format Fp32, intermediate data format must be Fp32");
                }
                log_assert(op_info.type != "maximum",  "gradient ops does not support maximum op");
            }

            // Constraint: Untilizing output buffer shared with iterm operand is not supported.
            if ((is_valid_matmul_op(op_info.type)) and (op_info.untilize_output)) {
                log_assert(
                    op_info.attributes.m_k == 1,
                    "untilizing output buffer shared with interm operand is not supported");
            } else if (
                (is_valid_reduce_op(op_info.type) and (op_info.attributes.reduce_type == ReduceFunc::Max) and
                 (op_info.attributes.reduce_dim == Dim::Z)) and
                (op_info.untilize_output)) {
                log_fatal("untilizing output of reduce max z dim op is not supported as interm operand is used");
            }

            // Constraint: gradient_op must be false for identity matmul
            if(is_valid_matmul_op(op_info.type) and op_info.attributes.identity) {
                log_assert(
                    op_info.gradient_op == 0,
                    "gradient accumulation for idenity matmul is not supported");
            }    

            if(is_valid_matmul_op(op_info.type) and op_info.attributes.identity) {

                if (op_info.input_tm_ops.find(2) != op_info.input_tm_ops.end()) {
                    for (int index=0; index<op_info.input_tm_ops.at(2).size(); index++) {
                        if (index == 0) {
                            if ((netlist_utils::get_valid_tm_op(op_info.input_tm_ops.at(2).at(index).first) == TmOp::rBroadcast) or
                                (netlist_utils::get_valid_tm_op(op_info.input_tm_ops.at(2).at(index).first) == TmOp::cBroadcast) or
                                (netlist_utils::get_valid_tm_op(op_info.input_tm_ops.at(2).at(index).first) == TmOp::zBroadcast)) {
                                    //log_assert(
                                    //    op_info.attributes.kernel_broadcast.size()>0 and (op_info.attributes.kernel_broadcast.at(2) > 0),
                                    //    "Matmul identity encoding input 2 supports only kernel broadcast!");
                            } else {
                                log_assert(
                                    false,
                                    "Matmul identity encoding input 2 supports only broadcast tm with kernel broadcast attribute!");

                            }
                        }    
                    }    
                }    
            }

            if(is_valid_reduce_op(op_info.type) and (op_info.attributes.reduce_dim == Dim::Z)) {
                log_assert(
                    op_info.input_data_formats.at(0) == op_info.intermed_data_format,
                    "Reduce Z dim does not support data format conversions, input df = {}, intermed df = {}, must set both to the same format", 
                    op_info.input_data_formats.at(0), op_info.intermed_data_format);

                log_assert(
                    op_info.output_data_format == op_info.intermed_data_format,
                    "Reduce Z dim does not support data format conversions, output df = {}, intermed df = {}, must set both to the same format", 
                    op_info.output_data_format, op_info.intermed_data_format);
            }

            if(is_valid_splice_op(op_info.type)) {
                for (uint i = 1; i < op_info.input_data_formats.size(); i++) {
                    log_assert(
                        op_info.input_data_formats.at(i-1) == op_info.input_data_formats.at(i),
                        "Splice op does not support data format conversions, input {} df = {}, input {} df = {}, must set both to the same format", 
                        i-1, op_info.input_data_formats.at(i-1), i, op_info.input_data_formats.at(i));
                }
            }
	    
            //WHB0: Add asserts for matmul l1 acc kernel
            if (op_info.attributes.l1_acc) {
                log_assert((device_info.arch == tt::ARCH::WORMHOLE_B0) || (device_info.arch == tt::ARCH::BLACKHOLE),
                    "L1 accumulation is only a feature of wormhole b0 matmul, not supported on other devices");

                log_assert(is_valid_matmul_op(op_info.type) || is_valid_depthwise_op(op_info.type),
                    "L1 accumulation is only a feature of wormhole b0 matmul, not supported on other ops");

                log_assert((op_info.intermed_data_format == DataFormat::Float32)
                    || (op_info.intermed_data_format == DataFormat::Int32)
                    || (op_info.intermed_data_format == DataFormat::Float16_b),
                    "wh_b0 hardware constraint: matmul l1 accumulation can only support accumulation in fp32 and float16_b");

            }

            // Int8 constraints
            bool is_input_int = false;
            for (const auto input_format : op_info.input_data_formats) {
                if ((input_format == DataFormat::Int8) || (input_format == DataFormat::Int32) || (input_format == DataFormat::UInt8) || (input_format == DataFormat::UInt16)) {
                    is_input_int = true;
                    break;
                }
            }

            const bool is_quant = op_info.type == "quantization";
            const bool is_requant = op_info.type == "requantization";
            const bool is_dequant = op_info.type == "dequantization";
            const bool is_fused_op = op_info.type == "fused_op";
            // TopK can use int inputs but is not configuring fpu to int mode.
            const bool is_top_k = op_info.type == "topk";

            // UInt8 constraints
            if (is_quant || is_requant || is_dequant || is_valid_reduce_op(op_info.type)) {
                for (const auto input_format : op_info.input_data_formats) {
                    log_assert(
                        input_format != DataFormat::UInt8 && op_info.output_data_format != DataFormat::UInt8,
                            "Quantization, (de/re)quantization and reduce ops cannot use UInt8 format");
                }
            }

            if (is_input_int && !is_top_k) {
                log_assert(
                    op_info.output_data_format == DataFormat::Int8 || op_info.output_data_format == DataFormat::Int32 || op_info.output_data_format == DataFormat::UInt16 ||
                        op_info.output_data_format == DataFormat::UInt8 || op_info.attributes.dequant || is_dequant || is_fused_op,
                    "If Int8, UInt8 or Int32 format is used, out data format must be Int8, UInt8 or Int32 if dequant is not enabled, "
                    "output format = {} is not Int8/UInt8/Int32",
                    op_info.output_data_format);

                if (op_info.attributes.dequant) {
                    log_assert(
                        !netlist_utils::is_int_format(op_info.output_data_format),
                        "If dequant is enabled, output format cannot be integer, output format = {} is integer",
                        op_info.output_data_format);
                }

                log_assert(
                    op_info.intermed_data_format == DataFormat::Int8 ||
                        op_info.intermed_data_format == DataFormat::UInt8 ||
                        op_info.intermed_data_format == DataFormat::Int32,
                    "If Int8, UInt8 or Int32 format is used, interm data formats in op must be Int8, UInt8 or Int32, "
                    "intermed_data_format = {} is not Int8/UInt8/Int32",
                    op_info.intermed_data_format);

                if (is_valid_binary_op(op_info.type) && !(is_quant || is_requant || is_dequant)) {
                    BinaryOp op_type = netlist_utils::get_valid_binary_op(op_info.type);
                    for (DataFormat input_format : op_info.input_data_formats) {
                        if (op_type == BinaryOp::Add || op_type == BinaryOp::Maximum) {
                            log_assert(
                                input_format == DataFormat::Int8 || input_format == DataFormat::UInt8 || input_format == DataFormat::Int32,
                                "Add and Max can have only Int8/Uint8 or Int32 inputs for integer version of this op input = {} is "
                                "not Int8/Uint8/Int32",
                                input_format);
                        }
                        else {
                            log_assert(
                                input_format == DataFormat::Int8 || input_format == DataFormat::UInt8,
                                "Add/Sub/Mul can have only Int8/UInt8 inputs for integer version of these ops input = {} is not "
                                "Int8/UInt8",
                                input_format);
                        }
                    }
                } else {
                    for (DataFormat input_format : op_info.input_data_formats) {
                        log_assert(
                            input_format == DataFormat::Int8 || input_format == DataFormat::UInt8 || input_format == DataFormat::UInt16 ||
                                input_format == DataFormat::Int32 || input_format == DataFormat::RawUInt32 ||
                                is_fused_op || is_requant || is_dequant || op_info.attributes.dequant || op_info.attributes.requant,
                            "If Int8, UInt8 or Int32 format is used, all input data formats in op must be Int8, UInt8 or Int32, input = "
                            "{} is not Int8/UInt8/Int32",
                            input_format);
                    }
                }

                log_assert(
                    !(op_info.attributes.requant && op_info.attributes.dequant),
                    "Requant and dequant can not be enabled for the same op");

                log_assert(
                    op_info.math_fidelity == MathFidelity::HiFi4, "If Int8, UInt8 or Int32 is used, fidelity must be Hifi4");

                log_assert(
                    op_info.dest_accumulate_data_format == DataFormat::Int32,
                    "If Int8, Uint8 or Int32 is used, dest accumulation format (acc_df) must be Int32");

                bool is_matmul = is_valid_matmul_op(op_info.type);
                bool is_ident_matmul = op_info.attributes.identity;
                if (is_matmul) {
                    log_assert(
                        op_info.input_data_formats[0] == DataFormat::Int8 || op_info.input_data_formats[0] == DataFormat::UInt8,
                        "Matmul with int inputs must have input 0 in Int8 or UInt8 format.");
                    log_assert(
                        op_info.input_data_formats[1] == DataFormat::Int8 || op_info.input_data_formats[0] == DataFormat::UInt8,
                        "Matmul with int inputs must have input 1 in Int8 or UInt8 format.");
                    
                    log_assert(!op_info.attributes.l1_acc, "Matmul with int inputs can't use l1 accumulation.");
                    if (op_info.attributes.bias) {
                        log_assert(
                            op_info.input_data_formats.at(2) == DataFormat::Int32,
                            "Matmul with int inputs can't have bias input in non-Int32 format.");
                    }
                    if (op_info.attributes.dequant || op_info.attributes.requant) {
                        int scalars_input_index = op_info.attributes.bias ? 3 : 2;
                        log_assert(
                            op_info.input_data_formats[scalars_input_index] == DataFormat::Float32,
                            "Matmul with int inputs must have scalars input in Float32 format.");
                    }

                    if (is_ident_matmul) {
                        log_assert(
                        op_info.input_names.size() == 3, "Matmul identity with int inputs can't have bias input.");
                    }

                    log_assert(op_info.attributes.min_input_buffer[1] == false, "Matmul with int inputs can't use min_buffer_input: 1.");
                }

                if (!is_matmul || (is_matmul && is_ident_matmul)) {
                    log_assert(
                        !(op_info.attributes.requant || op_info.attributes.dequant),
                        "Requant or dequant can be enabled only on non-identity matmul with int inputs");
                }

                if (op_info.attributes.relu_en) {
                    if (op_info.output_data_format == DataFormat::Int8) {
                        log_assert(std::abs(op_info.attributes.relu_threshold) < 127, "Relu threshold must be < 127");
                    } else if (op_info.output_data_format == DataFormat::Int32) {
                        log_assert(op_info.attributes.relu_threshold == 0, "Relu threshold must be 0 for Int32 output");
                        log_assert(op_info.attributes.relu_mode == ReluMode::Min, "Relu mode must be Min for Int32 output");
                    }
                }
            } else {
                log_assert(
                    op_info.dest_accumulate_data_format != DataFormat::Int32 || is_quant || is_fused_op,
                    "Dest accumulation format (acc_df) can be Int32 only if input format is Int8 or Int32 or op is "
                    "quantization or fused_op");
                log_assert(
                    !(op_info.attributes.requant || op_info.attributes.dequant),
                    "Requant or dequant can be enabled only when input format is Int8 or Int32");
            }

            if (is_dequant || is_requant) {
                log_assert(
                    op_info.input_data_formats.at(0) == DataFormat::Int32 || op_info.input_data_formats.at(0) == DataFormat::Int8,
                    "(de/re)quantization op must have input[0] data format Int32");
                log_assert(
                    op_info.input_data_formats.at(1) == DataFormat::Float32,
                    "(de/re)quantization op must have input[1] data format Float32");

                log_assert(
                    op_info.dest_accumulate_data_format == DataFormat::Int32,
                    "(de/re)quantization op must have dest accumulation data format Int32");
                if (is_dequant) {
                    log_assert(
                        op_info.output_data_format == DataFormat::Float32,
                        "dequantization op must have output data format Float32");
                } else {
                    log_assert(
                        op_info.output_data_format == DataFormat::Int8,
                        "requantization op must have output data format Int8");
                }
            }

            if (is_quant) {
                log_assert(
                    op_info.input_data_formats.at(0) == DataFormat::Float32 &&
                        op_info.input_data_formats.at(1) == DataFormat::Float32,
                    "quantization op must have all inputs data format Float32");
                log_assert(
                    op_info.output_data_format == DataFormat::Int8,
                    "quantization op must have output data format Int8");
                log_assert(
                    op_info.dest_accumulate_data_format == DataFormat::Int32,
                    "quantization op must have dest accumulation data format Int32");
            }
        }
    }

    // START: Verify Accumulation Ops have queue settings for their output queues which match expectation
    //      Find all ops that are gradient ops per graph.
    std::unordered_map<std::string, std::set<std::string>> list_of_gradient_ops_per_graph = {};
    for (const auto &graph_it : graph_map) {
        std::set<std::string> gradient_ops = {};
        for (const auto &op_it : graph_it.second.op_map) {
            tt_op_info op_info = op_it.second;
            if (op_it.second.gradient_op) {
                gradient_ops.insert(op_info.name);
            }
        }
        list_of_gradient_ops_per_graph.insert({graph_it.first, gradient_ops});
    }
    //      Find all queues that are connected to a gradient op.
    std::unordered_map<std::string, std::vector<std::string>> list_of_gradient_queues_per_graph = {};
    for (const auto &gradient_ops_it : list_of_gradient_ops_per_graph) {
        std::vector<std::string> gradient_queues = {};
        for (const auto &queue_it : queue_map) {
            for (const auto &gradient_op : gradient_ops_it.second) {
                if (queue_it.second.input == gradient_op) {
                    gradient_queues.push_back(queue_it.first);
                }
            }
        }
        list_of_gradient_queues_per_graph.insert({gradient_ops_it.first, gradient_queues});
    }
    //      Check all queue settings in a program match all queues found to be assosciated with a gradient op
    for (const auto &program_it : program_map) {
        for (const auto &instruction : program_it.second.program_trace) {
            if ((instruction.opcode == INSTRUCTION_OPCODE::Execute) and
                (list_of_gradient_ops_per_graph.find(instruction.graph_name) != list_of_gradient_ops_per_graph.end())) {
                for (const auto &gradient_queue : list_of_gradient_queues_per_graph.at(instruction.graph_name)) {
                    bool found = false;
                    for (const auto &queue_setting : instruction.queue_settings) {
                        if (queue_setting.name == gradient_queue) {
                            found = true;
                        }
                    }
                    if (not found) {
                        log_fatal(
                            "Gradient_op feeds output queue={} which requires an output queue_setting defined",
                            gradient_queue);
                    }
                }
            }
        }
    }
    // END: Verify Accumulation Ops have queue settings for their output queues which match expectation
    // START: Check each graph's input/output queue has a valid queue_setting
    std::unordered_map<std::string, std::unordered_set<std::string>> graph_io_queues_map = {};
    graph_io_queues_map.reserve(graph_map.size());

    for (const auto &graph_it : graph_map) {
        const string& graph_name = graph_it.first;
        graph_io_queues_map.insert({graph_name, {}});
        // All queues that get written to by graph
        for (const auto &queues_it : queue_map) {
            if (graph_it.second.op_map.find(queues_it.second.input) != graph_it.second.op_map.end()) {
                graph_io_queues_map.at(graph_name).insert(queues_it.first);
            }
        }
        // All queues used as input
        for (const auto &graph_op_it : graph_it.second.op_map) {
            int input_index = 0;
            for (const auto &op_input_name : graph_op_it.second.input_names) {
                if (graph_op_it.second.is_input_queue[input_index++]) {
                    graph_io_queues_map.at(graph_name).insert(op_input_name);
                }
            }
        }
    }

    for (const auto &program_it : program_map) {
        for (const auto &instruction : program_it.second.program_trace) {
            if ((instruction.opcode == INSTRUCTION_OPCODE::Execute)) {
                auto graph_io_queues_map_copy = graph_io_queues_map[instruction.graph_name];
                // Look up the graph
                for (const auto &queue_setting : instruction.queue_settings) {
                    if (graph_io_queues_map_copy.find(queue_setting.name) ==
                        graph_io_queues_map_copy.end()) {
                        log_fatal(
                            "program={} contains instruction with queue_setting for queue={} which isn't produced or "
                            "consumer in the graph={}",
                            program_it.first,
                            queue_setting.name,
                            instruction.graph_name);
                    }
                    graph_io_queues_map_copy.erase(queue_setting.name);
                }
                if (graph_io_queues_map_copy.size()) {
                    bool missing_queue_settings = false;
                    for (const auto &queue_name : graph_io_queues_map_copy) {
                        if (queue_map.at(queue_name).input == "HOST" and
                            queue_map.at(queue_name).type == IO_TYPE::Queue) {
                            log_error("queue={} is missing queue_setting", queue_name);
                            missing_queue_settings = true;
                        }
                    }
                    log_assert(
                        not missing_queue_settings,
                        "Missing queue_settings in execute instruction for graph={} program={}",
                        instruction.graph_name,
                        program_it.first);
                }
            }
        }
    }

    // END:
    // START: Check each op in a graph does not overlap cores
    for (const auto &graph_it : graph_map) {
        std::map<std::pair<int, int>, string> cores_used_for_op;
        for (const auto &op_it : graph_it.second.op_map) {
            if (is_non_tensix_op(op_it.second.type)) {
                // Non-tensix ops by default can stack on their cores
                continue;
            }
            for (int x = op_it.second.grid_loc_x(); x < (op_it.second.grid_loc_x() + op_it.second.grid_size_x()); x++) {
                for (int y = op_it.second.grid_loc_y(); y < (op_it.second.grid_loc_y() + op_it.second.grid_size_y());
                     y++) {
                    std::pair<int, int> coord({x, y});
                    if (cores_used_for_op.find(coord) != cores_used_for_op.end()) {
                        log_fatal(
                            "op={} is overlapping cores with another op={} in graph={}",
                            op_it.second.name,
                            cores_used_for_op.at(coord),
                            graph_it.first);
                    }
                    cores_used_for_op.insert({coord, op_it.second.name});
                }
            }
        }
    }
    // END:
    // START: Check each fused op in graphs points to a proper fused op and the number of inputs match
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            if (op_it.second.type == "fused_op") {
                const auto &fused_op_id = op_it.second.attributes.fused_op_id;
                log_assert(
                    fused_ops_unique_map.find(fused_op_id) != fused_ops_unique_map.end(),
                    "Cannot find the fused_op_id={} in the fused_ops section",
                    fused_op_id);
                log_assert(
                    fused_ops_unique_map.at(fused_op_id).num_inputs == op_it.second.input_names.size(),
                    "fused_op_id={} has inputs={} must be same as specified in number of inputs={} in op={}, graph={}",
                    fused_op_id,
                    fused_ops_unique_map.at(fused_op_id).num_inputs,
                    op_it.second.input_names.size(),
                    op_it.first,
                    graph_it.first);

                // Verify input dims
                for (const auto &dim_it : get_map_of_dim_info_per_input_for_op(op_it.first)) {
                    verify(dim_it.second);
                }
            }
        }
    }
    // END:

    // START:
    //   Also check if it is the output, then the dims must equal the base op
    std::unordered_map<std::string, bool> inputs_used = {};
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            if (netlist_utils::is_valid_fused_op(op_it.second.type)) {

                ////Eltwise binary ops with dest input and dest set as fp32 must have input formats also set as fp32
                ////since dest re-use has movd2a & movd2b ops for eltwise binary
                //const auto &op_info_ptr = op_it.second;
                //std::vector<tt_op_info> fused_op_info_vec = netlist_utils::get_list_of_tt_op_info_from_fused_op(op_info_ptr, fused_ops_map);
                //for(const auto &fused_op_info_ptr : fused_op_info_vec) {
                //    if(is_valid_binary_op(fused_op_info_ptr.type) && fused_op_info_ptr.dest_accumulate_data_format == DataFormat::Float32) {
                //        bool dest_used_as_input = false;
                //        for(const auto &input_name: fused_op_info_ptr.input_names) {
                //            if(input_name == "dest") {
                //                dest_used_as_input = true;
                //            }
                //        }

                //        if (dest_used_as_input) {
                //            for(const auto &input_format: op_it.second.input_data_formats) {
                //                log_assert(input_format == DataFormat::Float32, tt::LogNetlist,
                //                "Fused_op_id={} has eltwise binary op = {} with input from float32 dest, "
                //                "but input data format for eltwise binary is not set as float32.", op_it.first, fused_op_info_ptr.name);
                //            }
                //        }
                //    }
                //}

                const auto &fused_op = op_it.second.fused_op_info;
                // Check schedule
                for (const auto &schedule : fused_op.schedules) {
                    string previous_output = "";
                    for (const auto &scheduled_op : schedule.scheduled_ops) {
                        // Check inputs
                        bool dest_used_as_input = false;
                        for (const auto &input_name : scheduled_op.input_names) {
                            if (input_name.find("input") == 0) {
                                // Input forking is supported
                            } else if (input_name == "dest") {
                                dest_used_as_input = true;
                            }
                        }
                        if (previous_output == "dest") {
                            log_assert(
                                dest_used_as_input,
                                "fused_op_id={} scheduled_op={} needs to use dest as an input, since the previously "
                                "scheduled op writes to dest buffer",
                                fused_op.name,
                                scheduled_op.name);
                        }
                        if (dest_used_as_input) {
                            log_assert(
                                previous_output != "",
                                "fused_op_id={} scheduled_op={} uses dest as an input buffer but it is the first op in "
                                "the schedule",
                                fused_op.name,
                                scheduled_op.name);
                            log_assert(
                                previous_output == "dest",
                                "fused_op_id={} scheduled_op={} uses dest as an input buffer but previously scheduled "
                                "op doesn't write to dest",
                                fused_op.name,
                                scheduled_op.name);
                        }
                        // Check dims if output
                        bool is_output_op = (scheduled_op.output == "output");
                        log_assert(
                            (not is_output_op) or (scheduled_op.output_dim == op_it.second.output_dim),
                            "Scheduled_op={} in fused_op_id={} is output of schedule, but it's dimension doesn't "
                            "match "
                            "the base fused_op={}",
                            scheduled_op.name,
                            fused_op.name,
                            op_it.first);

                        log_assert(
                            not(is_output_op and netlist_utils::is_valid_matmul_op(scheduled_op.type) and
                                (scheduled_op.m_k > 1)),
                            "scheduled_op={} in fused_op_id={} cannot be output if it is a matmul with m_k > 1",
                            scheduled_op.name,
                            fused_op.name);
                        previous_output = scheduled_op.output;
                    }
                }
            }
        }
    }
    // END:

    // START:
    // Verify checks for tenstorrent/budabackend#913
    // All forked input names defined in an op must be a queue that exists or is defined:
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            std::unordered_set<string> inputs = {};
            for (const auto &input : op_it.second.input_names) {
                inputs.insert({input});
            }
            for (const auto &forked_dram_input_it : op_it.second.forked_dram_input_names) {
                string dram_input = forked_dram_input_it.first;
                string op_name = forked_dram_input_it.second;
                log_assert(
                    queue_map.find(dram_input) != queue_map.end(),
                    "dram_input={} specified in op={} graph={} must be an actual drama queue",
                    dram_input,
                    op_it.first,
                    graph_it.first);
                log_assert(
                    inputs.find(dram_input) != inputs.end(),
                    "dram_input={} specified in op={} graph={} must be an actual input to the op",
                    dram_input,
                    op_it.first,
                    graph_it.first);
                log_assert(
                    graph_it.second.op_map.find(op_name) != graph_it.second.op_map.end(),
                    "forked_dram_input={}:{} in op={} graph={} must belong in same graph as the op that is using the "
                    "fork",
                    dram_input,
                    op_name,
                    op_it.first,
                    graph_it.first);
            }
        }
    }
    // END:

    // START: if single-tile attribute is set, then there must be a broadcast in the input tm list
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            for (int index = 0; index < op_it.second.attributes.kernel_broadcast.size(); index++) {
                bool kernel_broadcast = (op_it.second.attributes.kernel_broadcast.at(index).first > 0);
                bool broadcast_found = false;
                if (op_it.second.input_tm_ops.find(index) != op_it.second.input_tm_ops.end()) {
                    for (const auto &tm : op_it.second.input_tm_ops.at(index)) {
                        if ((netlist_utils::get_valid_tm_op(tm.first) == TmOp::rBroadcast) or
                            (netlist_utils::get_valid_tm_op(tm.first) == TmOp::cBroadcast) or
                            (netlist_utils::get_valid_tm_op(tm.first) == TmOp::zBroadcast)) {
                            broadcast_found = true;
                            break;
                        }
                    }
                }
                log_assert(
                    not(kernel_broadcast and not broadcast_found),
                    "op={} has kernel_broadcast attribute set for input index={} but no valid broadcast tm for that input",
                    op_it.second.name,
                    index);
            }
        }
    }
    // END:

    // START: Fused ops mblock/ublocks need to be the same?

    // END:

    // START: if a queue is set to an alias, it must be a different queue that has the same volume and address
    for (const auto &queue_it : queue_map) {
        log_assert(
            queue_it.second.alias != queue_it.first,
            "IO={} Cannot alias to itself with alias={}",
            queue_it.first,
            queue_it.second.alias);
        if (not queue_it.second.alias.empty()) {
            log_assert(
                queue_map.find(queue_it.second.alias) != queue_map.end(),
                "IO={} is alias of IO={} which isn't defined in queues section",
                queue_it.first,
                queue_it.second.alias);
            log_assert(
                queue_map.at(queue_it.second.alias).alias == "",
                "IO={} cannot be an alias of IO={} which is already an alias",
                queue_it.first,
                queue_it.second.alias);
            log_assert(
                queue_it.second.type == IO_TYPE::RandomAccess,
                "IO={} cannot be an alias since it is not a ram",
                queue_it.first);
            log_assert(
                queue_map.at(queue_it.second.alias).type == IO_TYPE::RandomAccess,
                "IO={} cannot be an alias of IO={} which is not a ram",
                queue_it.first,
                queue_it.second.alias);
            log_assert(
                queue_map.at(queue_it.second.alias).alloc_info == queue_it.second.alloc_info,
                "IO={} cannot be an alias of IO={} unless its allocation information is the same",
                queue_it.first,
                queue_it.second.alias);

            int queue_tensor_size = get_tensor_size_in_elements(queue_it.second);
            int alias_queue_tensor_size = get_tensor_size_in_elements(queue_map.at(queue_it.second.alias));
            log_assert(
                alias_queue_tensor_size*queue_map.at(queue_it.second.alias).entries == queue_tensor_size*queue_it.second.entries,
                "IO={} has num_elements={} entries={} cannot be an alias of IO={} with num_elements={} entries={} unless its total number of elements is the same",
                queue_it.first, queue_tensor_size, queue_it.second.entries,
                queue_it.second.alias, alias_queue_tensor_size, queue_map.at(queue_it.second.alias).entries);
        }
    }
    // END

    // START: Checks for embedding ops -- tenstorrent/budabackend#1005
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            if (netlist_utils::is_valid_embedding_op(op_it.second.type)) {
                // Check num_indices need to be aligned to 32. 
                log_assert(
                    (op_it.second.attributes.num_indices % 32) == 0,
                    "graph={}, op={} is an embedding op where num_indices={} is not aligned to multiples of 32", 
                    graph_it.second.name, 
                    op_it.second.name, 
                    op_it.second.attributes.num_indices
                );
                // Check ublock_rt must be 1 to avoid complex reblock 
                log_assert(
                    op_it.second.output_dim.ublock_rt == 1,
                    "graph={}, op={} is an embedding op and must have output ublock_rt={} equal 1 to avoid complex reblock", 
                    graph_it.second.name, 
                    op_it.second.name, 
                    op_it.second.output_dim.ublock_rt
                );
                // Check ublock_rt must be 1 to avoid complex reblock 
                log_assert(
                    (op_it.second.attributes.num_indices / 32) == op_it.second.output_dim.mblock_m,
                    "graph={}, op={} is an embedding op and must have output num_indices={}/32 == mblock_m={}", 
                    graph_it.second.name, 
                    op_it.second.name, 
                    op_it.second.attributes.num_indices,
                    op_it.second.output_dim.mblock_m
                );
            }
        }
    }
    // START: Checks for tilizer op
    for (const auto &[graph_name, graph] : graph_map) {
        for (const auto &[op_name, op] : graph.op_map) {
            if (netlist_utils::is_valid_tilizer_op(op.type)) {

                for (const auto &tm_it : op.input_tm_ops) {
                    log_assert(
                        tm_it.second.empty(),
                        "graph={}, op={} is a tilizer op and should not have tm on input.",
                        graph_name, op_name
                    );
                }

                // Check that in/out ublock dims are the same if ublock_rt > 1
                const tt_dim_info& in0 = op.input_dims.at(0);
                const tt_dim_info& out = op.output_dim;

                if (in0.ublock_rt > 1 || out.ublock_rt > 1) {
                    log_assert(
                        out.ublock_rt == in0.ublock_rt && out.ublock_ct == in0.ublock_ct,
                        "graph={}, op={} for tilizer op input and output ublock dim has to match when ublock_rt>1, input ublock[rt,ct]=[{},{}] != output ublock[rt,ct]=[{},{}]", 
                        graph_name, op_name, in0.ublock_rt, in0.ublock_ct, out.ublock_rt, out.ublock_ct
                    );
                }    
            }
        }
    }
    // END

    // START: Checks for depthwise op
    {
        for (const auto& graph_it : graph_map) {
            const tt_graph_info& graph_info = graph_it.second;
            for (const auto& op_it : graph_info.op_map) {
                const tt_op_info& op_info = op_it.second;
                if (netlist_utils::is_valid_depthwise_op(op_info.type)) {
                    log_assert(op_info.attributes.u_kt == 1,
                        "graph={}, op={} is a depthwise op and must have u_kt == 1", 
                        graph_info.name, 
                        op_info.name);
                    log_assert(op_info.attributes.m_k > 0,
                        "graph={}, op={} is a depthwise op and must have m_k > 0", 
                        graph_info.name, 
                        op_info.name);
                }
            }
        }
    }
    // END

    // START: Checks for matmul ident ops -- tenstorrent/budabackend#1125
    for (const auto &graph_it : graph_map) {
        for (const auto &op_it : graph_it.second.op_map) {
            if (netlist_utils::is_valid_matmul_op(op_it.second.type) and op_it.second.attributes.identity) {
                // Check third input and df is raw. 
                log_assert(
                    op_it.second.input_names.size() >= 3,
                    "graph={}, op={} is a matmul identity op where num_inputs={} is < 3", 
                    graph_it.second.name, 
                    op_it.second.name, 
                    op_it.second.input_names.size()
                );
                log_assert(
                    (op_it.second.input_data_formats.at(2) == DataFormat::RawUInt8) or 
                    (op_it.second.input_data_formats.at(2) == DataFormat::RawUInt16) or 
                    (op_it.second.input_data_formats.at(2) == DataFormat::RawUInt32),
                    "graph={}, op={} is a matmul identity op where encodings df={} is not RawUInt8/RawUInt16/RawUInt32", 
                    graph_it.second.name, 
                    op_it.second.name, 
                    op_it.second.input_data_formats.at(2)
                );
            }
        }
    }
    // END

    // START: Checks buffered queues -- tenstorrent/budabackend#1200
    {
        // Get all buffered queues
        std::unordered_map<temporal_graph_id_t, std::set<std::string>> buffered_queue_names;
        for (const auto& [queue_name, queue_info] : queue_map) {
            if (is_queue_fed_by_op(queue_info)) {
                const auto& it_consumers = producer_consumer_map.find(queue_name);
                if (it_consumers != producer_consumer_map.end()) {
                    const auto& consumers = it_consumers->second;
                    if (consumers.size() == 1) {
                        const std::string& consumer_op_name = *consumers.begin();
                        const std::string& consumer_op_graph = op_graph_map.at(consumer_op_name);
                        const tt_op_info& op_info = graph_map.at(consumer_op_graph).op_map.at(consumer_op_name);
                        // buffered queue can be only one input of the op
                        if (std::count(op_info.input_names.begin(), op_info.input_names.end(), queue_name) == 1) {
                            const std::string& producer_op_graph = op_graph_map.at(queue_info.input);
                            const auto& producer_temporal_id = get_temporal_graph_of_graph(producer_op_graph);
                            const auto& consumer_temporal_id = get_temporal_graph_of_graph(consumer_op_graph);
                            if (producer_temporal_id == consumer_temporal_id) {
                                buffered_queue_names[producer_temporal_id].insert(queue_name);
                            }
                        }
                    }
                }
            }
        }

        if (buffered_queue_names.size() > 0)
        {
            // keep only buffered queues with more than 1 entry
            std::unordered_map<temporal_graph_id_t, std::set<std::string>> buffered_queue_names_tmp;
            for (const auto& [temporal_id, queue_names] : buffered_queue_names) {
                for (const auto& queue_name : queue_names) {
                    if (queue_map[queue_name].entries > 1) {
                        buffered_queue_names_tmp[temporal_id].insert(queue_name);
                    }
                }
            }

            buffered_queue_names = buffered_queue_names_tmp;

            if (buffered_queue_names.size() > 0) {
                std::vector<std::string> buffered_queues_not_dynamic;
                for (const auto &program_it : program_map) {
                    std::set<std::string> dynamic_queues;
                    for (const auto &instruction : program_it.second.program_trace) {
                        if (instruction.opcode == INSTRUCTION_OPCODE::AllocateQueue) {
                            for (const auto &var : instruction.vars) {
                                std::string q_name = std::get<0>(var);
                                dynamic_queues.insert(q_name);
                            }
                        }
                        if (instruction.opcode == INSTRUCTION_OPCODE::DeallocateQueue) {
                            for (const auto &var : instruction.vars) {
                                std::string q_name = std::get<0>(var);
                                dynamic_queues.erase(q_name);
                            }
                        }
                        if (instruction.opcode == INSTRUCTION_OPCODE::Execute) {
                            const auto& temporal_id = get_temporal_graph_of_graph(instruction.graph_name);
                            const auto& buffered_queues = buffered_queue_names[temporal_id];
                            if (buffered_queues.size() > 0) {
                                // Warning if read pointer auto inc > 1 for buffered queues
                                for (const auto& queue_setting : instruction.queue_settings) {
                                    if (queue_setting.rd_ptr_autoinc != "" && queue_setting.rd_ptr_autoinc != "1") {
                                        if (buffered_queues.find(queue_setting.name) != buffered_queues.end()) {
                                            log_warning(tt::LogNetlist, "Queue {} is buffered but read pointer auto inc > 1", queue_setting.name);
                                        }
                                    }
                                }
                                // Find buffered queues that are not dynamicly allocated
                                std::set_difference(
                                    buffered_queues.begin(), buffered_queues.end(),
                                    dynamic_queues.begin(), dynamic_queues.end(),
                                    std::back_inserter(buffered_queues_not_dynamic));
                            }
                        }
                    }
                }

                if (buffered_queues_not_dynamic.size() > 0) {
                    log_warning(
                        tt::LogNetlist,
                        "Missing explicitely to allocate queue for ({}) buffered queue(s) with more than 1 entry.",
                        buffered_queues_not_dynamic.size());

                    for ([[maybe_unused]] const auto& queue_name : buffered_queues_not_dynamic) {
                        log_debug(tt::LogNetlist, "Queue {} is not dynamically allocated", queue_name);
                    }
                }
            }
        }
    }
    // END
}

//! This function is for fused_ops to get the dimensions of each of the inputs specified.
//  It also doubles to check for error cases such as duplicate op names
std::unordered_map<std::string, tt_dim_info> netlist_parser::get_map_of_dim_info_per_input_for_op(
    const string &op_name) {
    std::unordered_map<std::string, tt_dim_info> results = {};
    for (const auto &graph_it : graph_map) {
        const auto &graph = graph_it.second;
        if (graph.op_map.find(op_name) != graph.op_map.end()) {
            // Found the op
            log_assert(
                results.empty(),
                "Results have to be empty when the op is found, otherwise this means there is duplicate op names, "
                "which is problematic for wormhole");
            const auto &op_info = graph.op_map.at(op_name);
            for (int index = 0; index < op_info.input_names.size(); index++) {
                const string &input_name = op_info.input_names.at(index);
                const string &relative_input_name = "input" + to_string(index);
                bool is_queue_input = queue_map.find(input_name) != queue_map.end();
                if (is_queue_input) {
                    results.insert({relative_input_name, queue_map.at(input_name).dim});
                } else {
                    for (const auto &search_graph_it : graph_map) {
                        if (search_graph_it.second.op_map.find(input_name) != search_graph_it.second.op_map.end()) {
                            const auto &producer_op = search_graph_it.second.op_map.at(input_name);
                            //
                            log_assert(
                                results.find(relative_input_name) == results.end(),
                                "Results entry for input_name={} have to be empty when the op is found, otherwise this "
                                "means there is duplicate op names, which is problematic for wormhole");
                            results.insert({relative_input_name, producer_op.output_dim});
                        }
                    }
                }
            }
        }
    }

    return results;
}
