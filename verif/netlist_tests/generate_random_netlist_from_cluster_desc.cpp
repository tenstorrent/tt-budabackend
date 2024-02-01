// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "netlist_info_types.hpp"
#include "device/tt_cluster_descriptor.h"
#include "common/buda_soc_descriptor.h"
#include "verif.hpp"

#include <fstream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <array>
#include <ostream>
#include <cstdlib>

constexpr int WORMHOLE_WORKER_GRID_SHAPE_X = 8;
constexpr int WORMHOLE_WORKER_GRID_SHAPE_Y = 10;

struct program_args_t {
    int seed;
    std::string cluster_desc_path;
    std::string output_file_path;
    bool allow_op_chains;
    int input_count;

    program_args_t() : seed(-1), cluster_desc_path(""), output_file_path(""), allow_op_chains(false), input_count(1) {}
};


struct netlist_t {
    tt_program_info program;
    std::unordered_map<std::string, tt_queue_info> queue_map;
    std::unordered_map<chip_id_t, tt_graph_info> graphs;

    netlist_t(const std::vector<chip_id_t> &chip_ids, int input_count) : program(), queue_map() {
        for (auto id : chip_ids) {
            graphs.insert({id, {"graph_for_chip_" + std::to_string(id), id, input_count, {}}});
        }
    }
};


std::array<std::string, 4> unary_op_types = {
    "datacopy", "exp", "gelu", "sqrt"
};
std::string rand_unary_op_type() {
    return unary_op_types.at(rand() % unary_op_types.size());
}
std::array<std::string, 3> binary_op_types = {
    "add", "multiply", "subtract"
};
std::string rand_binary_op_type() {
    return binary_op_types.at(rand() % binary_op_types.size());
}


// template <typename T>
// void emit_vector_to_stream(std::basic_ostream &os, const std::vector<T> &vec) {
//     int num = vec.size();
//     for (int i = 0; i < num; i++) {
//         const auto &val = vec[i];
//         os << val;
//         if (i < num - 1) {
//             os << ",";
//         }
//     }
// }

program_args_t parse_args(std::vector<std::string> input_args) {
    auto args = program_args_t();

    bool help = false;
    string help_string;
    help_string += "generate_random_netlist_from_cluster_desc --output-dir [output-directory] --cluster-desc [path to cluster description]\n";
    help_string += "--output-dir <>             : Path to output netlist file\n";
    help_string += "--cluster-desc <>           : Path to cluster description file\n";
    help_string += "--enable-op-chains          : If specified, will allow ops that don't either read from DRAM or write to DRAM\n";
    help_string += "--seed <>                   : Random seed\n";
    help_string += "--help                      : Prints this message\n";
    try {
        std::tie(args.seed, input_args) = verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.cluster_desc_path, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--cluster-desc");
        std::tie(args.output_file_path, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--output-dir");
        std::tie(args.allow_op_chains, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--allow-op-chains");
        std::tie(help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        verif_args::validate_remaining_args(input_args);
    } catch (const std::exception& e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (help) {
        log_info(tt::LogNetlist, "Usage Help:\n{}", help_string);
        exit(0);
    }

    return args;
}


auto get_available_ethernet_links_between_chip_pairs(const tt_ClusterDescriptor &cluster_desc) {
    // Available streams per chip-to-chip pair.
    // Adopt a canonical form where the first entry is always the lower ID of the pair of chip IDs
    auto available_eth_links = std::unordered_map<chip_id_t, int>{};
    const auto &ethernet_connections = cluster_desc.get_ethernet_connections();
    for (auto id : cluster_desc.get_all_chips()) {
        for (const auto &[src_channel, dest_chip_and_channel] : ethernet_connections.at(id)) {
            chip_id_t dest_chip = std::get<0>(dest_chip_and_channel);
            available_eth_links[id] += 1;
        }
    }

    return available_eth_links;
}

tt_op_info choose_op_info_template_shape() {
    const int ublock_rt = 4;
    const int ublock_ct = 2;
    const int mblock_m = 4;
    const int mblock_n = 8;

    auto valid_grid_shapes = std::vector<tt_xy_pair> {
        // {1,8},
        // {2,4},
        {4,2},
        {8,1},
    };

    auto grid_shape = valid_grid_shapes.at(rand() % valid_grid_shapes.size());
    return tt_op_info{
        .name="",
        .type="",
        .grid_loc={0,0},
        .grid_size={
            .r = static_cast<uint32_t> (grid_shape.y), 
            .c = static_cast<uint32_t> (grid_shape.x)
        },
        .input_names={},
        .input_data_formats={DataFormat::Float16},
        .intermed_data_format=DataFormat::Float16,
        .dest_accumulate_data_format=DataFormat::Float16,
        .output_data_format=DataFormat::Float16,
        .output_dim = {
            .t=1,
            .ublock_rt=ublock_rt,
            .ublock_ct=ublock_ct,
            .mblock_m=mblock_m,
            .mblock_n=mblock_n,
            .ublock_order=Dim::R,
        },
        .buf_size_mb=-1,
        .input_buf_min_size_tiles={ublock_ct*ublock_rt}
    };

}


tt_op_info create_op_from_template(chip_id_t chip, tt_xy_pair &location, const tt_op_info template_op_info, const std::vector<std::pair<std::string, chip_id_t>> &producer_ops, netlist_t &netlist, int unique_id) {
    // tt_op_info new_op = template_op_info;

    auto name_ss = std::stringstream();
    name_ss << "chip_" << chip << "_op_" << unique_id;
    int num_producers = producer_ops.size();
    if (num_producers) {
        name_ss << "_consumer_of_";
        for (int i = 0; i < num_producers; i++) {
            const auto &n = producer_ops[i].first;

            name_ss << n;
            if (i < num_producers - 1) {
                name_ss << "__";
            } 
        }
    }

    netlist.graphs.at(chip).op_map.insert({name_ss.str(), tt_op_info()});
    auto &new_op = netlist.graphs.at(chip).op_map.at(name_ss.str());
    new_op = template_op_info;
    new_op.grid_loc = {static_cast<int>(location.y), static_cast<int>(location.x)};
    if (num_producers) {
        for (int i = 0; i < num_producers; i++) {
            const auto &n = producer_ops[i].first;
            new_op.input_names.push_back(n);
        }
    }

    while (new_op.input_data_formats.size() < producer_ops.size()) {
        auto idx = new_op.input_data_formats.size();
        const auto &[producer_name, producer_chip] = producer_ops.at(idx);
        auto producer_data_format = netlist.graphs.at(producer_chip).op_map.at(producer_name).output_data_format;
        new_op.input_data_formats.push_back(producer_data_format);
    }
    new_op.name = name_ss.str();

    log_assert(&new_op.type != &template_op_info.type, "new op type should not match template op type");
    if (producer_ops.size() == 2) {
        new_op.type = rand_binary_op_type();
    } else if (producer_ops.size() == 1) {
        new_op.type = rand_unary_op_type();
    } else if (producer_ops.size() > 2) {
        log_fatal("Num producers cannot be > 2");
    }
    

    log_assert(new_op.grid_loc.at(0) + new_op.grid_size.at(0) <= 10, "Op exceeds grid dimension");

    return new_op;
}

std::tuple<std::vector<std::string>, tt_xy_pair> generate_consumer_ops(
    chip_id_t chip, 
    const std::vector<std::pair<std::string, chip_id_t>> &incoming_op_outputs, 
    const tt_op_info &template_op_info, 
    netlist_t &netlist, 
    int max_consumer_ops
) {
    const tt_xy_pair &op_grid_shape = tt_xy_pair(template_op_info.grid_size.at(1), template_op_info.grid_size.at(0));
    const tt_xy_pair &grid_start = {0,0};
    tt_xy_pair current_location = grid_start;
    auto consumer_op_names = std::vector<std::string>();
    int id = 0;
    int num_binaries_to_instantiate = std::max(0, static_cast<int>(incoming_op_outputs.size()) - max_consumer_ops);
    std::string last_op_name = "";
    std::string last_op_type = "";
    for (int i = 0; i < incoming_op_outputs.size(); i++) {
        std::vector<std::pair<std::string, chip_id_t>> input_ops = {incoming_op_outputs[i]};

        if (num_binaries_to_instantiate > 0) {
            i++;
            input_ops.push_back(incoming_op_outputs.at(i));
            num_binaries_to_instantiate--;
        }
        log_assert(current_location.y + op_grid_shape.y <= 10, "Op exceeds grid dimension");
        const auto &consumer_op = create_op_from_template(chip, current_location, template_op_info, input_ops, netlist, id);
        log_assert(consumer_op.type.size() > 0, "Must have at least one consumer");
        if (last_op_name.size() > 0) {
            log_assert(netlist.graphs.at(chip).op_map.at(last_op_name).type.compare(last_op_type) == 0, "Last op type set incorrectly");
        }
        last_op_name = consumer_op.name;
        last_op_type = consumer_op.type;
        consumer_op_names.push_back(consumer_op.name);
        // Assumes all ops are the same grid size
        if (WORMHOLE_WORKER_GRID_SHAPE_X <= current_location.x + op_grid_shape.x) {
            current_location.x = grid_start.x;
            current_location.y += op_grid_shape.y;
        } else {
            current_location.x += op_grid_shape.x;
        }
        id++;
    }

    return {consumer_op_names, current_location};
}


std::vector<std::string> generate_producer_ops(chip_id_t chip, const tt_xy_pair &producer_ops_grid_start, int op_count, const tt_op_info &template_op_info, netlist_t &netlist) {
    const tt_xy_pair &op_grid_shape = tt_xy_pair(template_op_info.grid_size.at(1), template_op_info.grid_size.at(0));
    tt_xy_pair current_location = producer_ops_grid_start;
    auto producer_op_names = std::vector<std::string>();
    int i = 0;
    for (; i < op_count; i++) {
        if (current_location.x + op_grid_shape.x > WORMHOLE_WORKER_GRID_SHAPE_X || current_location.y + op_grid_shape.y > WORMHOLE_WORKER_GRID_SHAPE_Y) {
            break;
        }
        const auto &producer_op = create_op_from_template(chip, current_location, template_op_info, {}, netlist, i);
        producer_op_names.push_back(producer_op.name);
        // Assumes all ops are the same grid size
        if (WORMHOLE_WORKER_GRID_SHAPE_X <= current_location.x + op_grid_shape.x) {
            current_location.x = producer_ops_grid_start.x;
            current_location.y += op_grid_shape.y;
        } else {
            current_location.x += op_grid_shape.x;
        }
    }

    int num_placed_output_ops = producer_op_names.size();
    for (; i < op_count; i++) {
        producer_op_names.push_back(producer_op_names.at(i % num_placed_output_ops)); // fork to saturate the output eth links
    }

    return producer_op_names;
}


std::vector<chip_id_t> identify_consumer_chips_for_producer_ops(chip_id_t chip, const std::unordered_map<chip_id_t,std::unordered_map<ethernet_channel_t,std::tuple<chip_id_t,ethernet_channel_t>>> &eth_connections, const std::vector<std::pair<std::string, chip_id_t>> &incoming_op_outputs) {
    auto consumer_chips = std::vector<chip_id_t>();
    
    auto neighbour_link_counts = std::unordered_map<chip_id_t, int>();
    const auto &connected_links = eth_connections.at(chip);
    for (const auto &[src_channel, dest_chip_and_channel] : connected_links) {
        neighbour_link_counts[std::get<0>(dest_chip_and_channel)] += 1;
    }

    for (const auto &[op_name, chip_id] : incoming_op_outputs) {
        log_assert(neighbour_link_counts[chip_id] > 0, "Must have at least one link to neigbor");
        neighbour_link_counts[chip_id] -= 1;
    }

    auto consumer_chips_for_producer_ops = std::vector<chip_id_t>();
    for (const auto &[chip, num_available_links] : neighbour_link_counts) {
        for (int i = 0; i < num_available_links; i++) {
            consumer_chips_for_producer_ops.push_back(chip);
        }
    }
    
    return consumer_chips_for_producer_ops;
}

tt_queue_info initialize_queue_shape_data_from_op(const tt_op_info &op, chip_id_t chip, int input_count) {
    const auto &q_name = "input_queue_" + op.name;
    auto queue_info = tt_queue_info{
        .name=q_name,
        .target_device=chip,
        .entries=input_count,
        .grid_size={
            .r=op.grid_size.at(0),
            .c=op.grid_size.at(1),
        },
        .input_count=input_count,
        .dim=op.output_dim,
        .data_format=op.input_data_formats.at(0),
        .type=IO_TYPE::Queue
        // .loc=QUEUE_LOCATION::DRAM,
    };        
    return queue_info;
}

const std::vector<std::vector<int>> dram_channels_on_worker_row = {
    {0,2},
    {3},
    {4},
    {4},
    {1,5},
    {1,5},
    {4},
    {3},
    {3},
    {0,2}
};

void initialize_input_queue_alloc_info(tt_queue_info &queue_info, const tt_op_info &consumer_op, std::unordered_map<int, std::size_t> &dram_channel_offsets) {
    int num_channels = dram_channel_offsets.size();
    int num_allocations = queue_info.grid_size.r * queue_info.grid_size.c;
    auto alloc_size = queue_info.entries * get_tensor_size_in_bytes(queue_info, true);
    for (int r = 0; r < queue_info.grid_size.r; r++) {
        for (int c = 0; c < queue_info.grid_size.c; c++) {
            auto consumer_row = consumer_op.grid_loc.at(0) + r;
            const auto &channels = dram_channels_on_worker_row.at(consumer_row);
            uint32_t channel = channels.at(rand() % channels.size());
            queue_info.alloc_info.push_back({.channel=channel, .address=static_cast<uint32_t>(dram_channel_offsets.at(channel))});

            dram_channel_offsets[channel] += alloc_size;
        }
    }
    // for (int i = 0; i < num_allocations; i++) {
    // }
}

void initialize_queue_alloc_info(tt_queue_info &queue_info, std::unordered_map<int, std::size_t> &dram_channel_offsets) {
    int num_channels = dram_channel_offsets.size();
    int num_allocations = queue_info.grid_size.r * queue_info.grid_size.c;
    auto alloc_size = queue_info.entries * get_tensor_size_in_bytes(queue_info, true);
    for (int i = 0; i < num_allocations; i++) {
        uint32_t channel = rand() % num_channels;

        // this cast will be unsafe once our dram channels have > 4GB of memory ---------v
        queue_info.alloc_info.push_back({.channel=channel, .address=static_cast<uint32_t>(dram_channel_offsets.at(channel))});
        dram_channel_offsets[channel] += alloc_size;
    }
}

std::pair<std::vector<std::string>, std::vector<std::string>> generate_queue_data(
    const std::vector<chip_id_t> &chip_ids, 
    netlist_t &netlist, 
    const std::vector<std::string> &input_ops, 
    const std::vector<std::string> &output_ops, 
    const std::unordered_map<std::string, chip_id_t> &op_chip,
    int input_count
) {
    std::unordered_map<chip_id_t, std::unordered_map<int, std::size_t>> chip_dram_channel_offsets;
    for (auto chip_id : chip_ids) {
        chip_dram_channel_offsets[chip_id][0] = 0x4200000;
        chip_dram_channel_offsets[chip_id][1] = 0x4200000;
        chip_dram_channel_offsets[chip_id][2] = 0x4200000;
        chip_dram_channel_offsets[chip_id][3] = 0x4200000;
        chip_dram_channel_offsets[chip_id][4] = 0x4200000;
        chip_dram_channel_offsets[chip_id][5] = 0x4200000;
    }


    for (const auto &op_name : input_ops) {
        chip_id_t chip = op_chip.at(op_name);
        const auto &op = netlist.graphs.at(chip).op_map.at(op_name);
        log_assert(op.input_names.size() == 0, "Op must have at least one input");
    }

    std::vector<std::string> input_queues;
    for (const auto &op_name : input_ops) {
        chip_id_t chip = op_chip.at(op_name);
        auto &consumer_op = netlist.graphs.at(chip).op_map.at(op_name);
        auto queue_info = initialize_queue_shape_data_from_op(consumer_op, chip, input_count);
        queue_info.input="HOST";
        queue_info.entries=input_count;
        queue_info.input_count=input_count;
        queue_info.loc=QUEUE_LOCATION::DRAM;
        initialize_input_queue_alloc_info(queue_info, consumer_op, chip_dram_channel_offsets.at(chip));
        consumer_op.input_names.push_back(queue_info.name);

        netlist.queue_map.insert({queue_info.name, queue_info});
        input_queues.push_back(queue_info.name);
    }

    std::vector<std::string> output_queues;
    for (const auto &op_name : output_ops) {
        chip_id_t chip = op_chip.at(op_name);
        const auto &producer_op = netlist.graphs.at(chip).op_map.at(op_name);
        auto queue_info = initialize_queue_shape_data_from_op(producer_op, chip, input_count);
        queue_info.input=op_name;
        queue_info.entries=input_count;
        queue_info.input_count=input_count;
        queue_info.loc=QUEUE_LOCATION::DRAM;
        initialize_queue_alloc_info(queue_info, chip_dram_channel_offsets.at(chip));

        netlist.queue_map.insert({queue_info.name, queue_info});
        output_queues.push_back(queue_info.name);
    }

    return {input_queues, output_queues};
}

void export_netlist_to_yaml(const netlist_t &netlist, const std::string &out_path) {
    auto input_queues_per_chip = std::unordered_map<chip_id_t, std::vector<std::string>>();
    for (const auto &[name, queue] : netlist.queue_map) {
        input_queues_per_chip[queue.target_device].push_back(name);
    }

    auto yaml_s = std::ofstream(out_path);

    yaml_s << "devices:" << std::endl;
    yaml_s << "  arch: wormhole" << std::endl << std::endl;
    yaml_s << "queues:\n";
    for (const auto &[name, q] : netlist.queue_map) {
        const auto &q_loc = (q.loc == QUEUE_LOCATION::HOST ? "host": "dram");
        yaml_s << "  " << name << ": {";
        yaml_s << "type: " << (q.type == IO_TYPE::Queue ? "queue" : "ram") << ", input: " << q.input << ", entries: " << q.entries << ", grid_size: [" << q.grid_size.r << ", " << q.grid_size.c << "]";
        yaml_s << ", t: " << q.dim.t << ", mblock: [" << q.dim.mblock_m << ", " << q.dim.mblock_n << "], ublock: [" << q.dim.ublock_rt << ", " << q.dim.ublock_ct << "]";
        yaml_s << ", df: " << q.data_format << ", target_device: " << q.target_device << ", loc: " << q_loc;
        yaml_s << ", " << q_loc << ": [";
        int num_allocs = q.alloc_info.size();
        for (int i = 0; i < num_allocs; i++) {
            const auto &alloc_info = q.alloc_info[i];
            yaml_s << "[" << alloc_info.channel << ", 0x" << std::hex << alloc_info.address << std::dec << "]";
            if (i < num_allocs - 1) {
                yaml_s << ", ";
            }
        }

        yaml_s << "]}\n";
    }

    yaml_s << "\n";
    yaml_s << "graphs:\n";
    for (const auto &[chip, graph] : netlist.graphs) {
        yaml_s << "  " << graph.name << ":\n";
        yaml_s << "    target_device: " << graph.target_device << "\n";
        yaml_s << "    input_count: " << graph.input_count << "\n";
        for (const auto &[op_name,op_info] : graph.op_map) {
            yaml_s << "    " << op_name << ": {type: " << op_info.type << ", grid_loc: [" << op_info.grid_loc.at(0) << "," << op_info.grid_loc.at(1) << "]";
            yaml_s << ", grid_size: [" << op_info.grid_size.at(0) << ", " << op_info.grid_size.at(1) << "]";
            yaml_s << ", inputs: [";
            // emit_vector_to_stream(yaml_s, op_info.input_names);
            int num_inputs = op_info.input_names.size();
            for (int i = 0; i < num_inputs; i++) {
                yaml_s << op_info.input_names[i];
                if (i < num_inputs - 1) {
                    yaml_s << ",";
                }
            }
            yaml_s << "], acc_df: " << op_info.dest_accumulate_data_format << ", in_df: [";
            // emit_vector_to_stream(yaml_s, op_info.input_data_formats);
            for (int i = 0; i < num_inputs; i++) {
                yaml_s << op_info.input_data_formats[i];
                if (i < num_inputs - 1) {
                    yaml_s << ",";
                }
            }
            yaml_s << "], out_df: " << op_info.output_data_format << ", ublock_order: r, buf_size_mb: 2,  intermed_df: " << op_info.intermed_data_format;
            yaml_s << ", math_fidelity: HiFi3, untilize_output: false, t: " << op_info.output_dim.t << ", mblock: [" << op_info.output_dim.mblock_m << ", " << op_info.output_dim.mblock_n << "]";
            yaml_s << ", ublock: [" << op_info.output_dim.ublock_rt << ", " << op_info.output_dim.ublock_ct << "], input_0_tms: []}\n";
        }
        yaml_s << "\n";
    }

    yaml_s << "programs:\n";
    yaml_s << "  - program0:\n";
    yaml_s << "    - var: [$0, $1, $5, $8]\n";
    yaml_s << "    - varinst: [$5, set, 1] # LOAD an 8 into $5\n";
    yaml_s << "    - varinst: [$8, set, 1] # LOAD a  1 into $8 \n";
    yaml_s << "    - loop: $5\n";
    for (const auto &[chip_id, graph] : netlist.graphs) {
        yaml_s << "    - execute: {graph_name: " << graph.name << ", queue_settings: {\n";
        int num_input_queues = input_queues_per_chip.at(chip_id).size();
        for (int i = 0; i < num_input_queues; i++) {
            const auto &q_name = input_queues_per_chip.at(chip_id).at(i);
            yaml_s << "      " << q_name << ": {prologue: false, epilogue: false, zero: false, rd_ptr_local: $0, rd_ptr_global: $0}";
            if (i < num_input_queues - 1) {
                yaml_s << ",\n";
            } else {
                yaml_s << "}}\n";
            }
        }
        yaml_s << "\n";
    }
    yaml_s << "    - varinst: [$0, add, $0, $8] # add two variables\n";
    yaml_s << "    - varinst: [$1, add, $1, $8] # add two variables\n";
    yaml_s << "    - endloop: \n";

    yaml_s.close();
}

std::string generate_invocation_command(const std::vector<std::string> &input_queues, const std::vector<std::string> &output_queues, const std::string &netlist_path) {
    auto command = std::stringstream();
    command << "./build/test/verif/netlist_tests/test_inference --netlist " << netlist_path << " --backend Silicon --seed 0 ";
    command << " --inputs ";
    // emit_vector_to_stream(command, input_queues);
    int num_inputs = input_queues.size();
    for (int i = 0; i < num_inputs; i++) {
        const auto &n = input_queues[i];
        command << n;
        if (i < num_inputs - 1) {
            command << ",";
        }
    }
    command << " --outputs ";
    // emit_vector_to_stream(command, output_queues);
    int num_outputs = output_queues.size();
    for (int i = 0; i < num_outputs; i++) {
        const auto &n = output_queues[i];
        command << n;
        if (i < num_outputs - 1) {
            command << ",";
        }
    }
    
    command << " --cluster-desc eth_connections.yaml";
    // std::cout << "RUN COMMAND: " << command.str();

    return command.str();
}

void convert_unary_op_to_binary(tt_op_info &op, chip_id_t chip, netlist_t &netlist) {
    op.type = rand_binary_op_type();
    op.input_data_formats.clear();
    std::transform(op.input_names.begin(), op.input_names.end(), std::back_inserter(op.input_data_formats), [&netlist, &chip](const std::string &n) { return netlist.graphs.at(chip).op_map.at(n).output_data_format; });
}

std::tuple<netlist_t, std::vector<std::string>, std::vector<std::string>> generate_netlist_from_cluster_desc(const tt_ClusterDescriptor &cluster_desc, const program_args_t &args) {
    const auto &chips = cluster_desc.get_all_chips();
    const auto &chips_vec = std::vector<chip_id_t>(chips.begin(), chips.end());
    auto netlist = netlist_t(chips_vec, args.input_count);

    // Available streams per chip-to-chip pair.
    // Adopt a canonical form where the first entry is always the lower ID of the pair of chip IDs
    auto chip_eth_links_available = get_available_ethernet_links_between_chip_pairs(cluster_desc);

    const tt_op_info template_op_info = choose_op_info_template_shape();

    auto chips_sorted_by_ascending_number_of_ethernet_streams = std::vector<chip_id_t>(chips.begin(), chips.end());
    const auto &eth_connections = cluster_desc.get_ethernet_connections();

    // TODO(snijjar): Change over to a heap instead and heapify based on `chip_eth_links_available` so we are always updating the correct chip
    // Although this current approach is probably fine for 12 chip systems
    auto eth_link_count_compare = [&eth_connections](chip_id_t a, chip_id_t b) { return eth_connections.at(a).size() > eth_connections.at(b).size(); };
    std::make_heap(chips_sorted_by_ascending_number_of_ethernet_streams.begin(), chips_sorted_by_ascending_number_of_ethernet_streams.end(), eth_link_count_compare);
    // std::sort(chips_sorted_by_ascending_number_of_ethernet_streams.begin(), chips_sorted_by_ascending_number_of_ethernet_streams.end(), [&eth_connections](chip_id_t a, chip_id_t b) { return eth_connections.at(a).size() < eth_connections.at(b).size(); });
    log_assert(eth_connections.at(chips_sorted_by_ascending_number_of_ethernet_streams.front()).size() <= eth_connections.at(chips_sorted_by_ascending_number_of_ethernet_streams.back()).size(), "Incorrect ascending order of chips sorted by number of ethernet streams");

    constexpr int OPS_PER_CHIP = 10;

    auto op_chip = std::unordered_map<std::string, chip_id_t>();
    auto output_ops = std::unordered_set<std::string>();
    auto input_ops = std::unordered_set<std::string>();
    auto chip_incoming_op_outputs = std::unordered_map<chip_id_t, std::vector<std::pair<std::string, chip_id_t>>>();
    int consumer_to_producer_select_index = 0;
    while (chips_sorted_by_ascending_number_of_ethernet_streams.size() > 0) {
        auto chip = chips_sorted_by_ascending_number_of_ethernet_streams.front();
        // for (auto chip : chips_sorted_by_ascending_number_of_ethernet_streams) {
        const auto &incoming_op_outputs = chip_incoming_op_outputs[chip];
        int used_eth_links_for_incoming_data = incoming_op_outputs.size();
        // create `available_output_eth_links` number of 8 core ops that will produce data for the neigbouring chips
        int available_output_eth_links = chip_eth_links_available.at(chip);
        log_debug(tt::LogTest, "chip: {} with {} initial eth links. {} used eth links => {} links available", chip, eth_connections.at(chip).size(), used_eth_links_for_incoming_data, available_output_eth_links);

        int max_consumer_ops = std::min<int>(std::max<int>(used_eth_links_for_incoming_data, OPS_PER_CHIP - available_output_eth_links), ((used_eth_links_for_incoming_data - 1) / 2) + 1); 
        int max_producer_ops = OPS_PER_CHIP - max_consumer_ops;
        // Generate the consumer ops
        log_debug(tt::LogTest,"\tmax_consumer_ops: {}, num_producer_ops: {}", max_consumer_ops, chip_incoming_op_outputs.at(chip).size());
        const auto &[consumer_ops, producer_ops_grid_start] = generate_consumer_ops(chip, chip_incoming_op_outputs.at(chip), template_op_info, netlist, max_consumer_ops);
        // Update input/output op tracking
        log_debug(tt::LogTest,"\thas {} consuming ops (8 cores each):", consumer_ops.size());
        log_debug(tt::LogTest,"\t\t{}", consumer_ops);
        for (const auto &consumer_op : consumer_ops) {
            op_chip[consumer_op] = chip;
            output_ops.insert(consumer_op);
        }
        for (const auto &[producer_op, producer_chip] : chip_incoming_op_outputs.at(chip)) {
            output_ops.erase(producer_op);
        }
        

        // create the producer ops (optionally connect them to the inputs)
        const auto &producer_ops = generate_producer_ops(chip, producer_ops_grid_start, available_output_eth_links, template_op_info, netlist);
        log_debug(tt::LogTest,"\thas {} producer ops (8 cores each):", producer_ops.size());    
        log_debug(tt::LogTest,"\t\t{}", producer_ops);
        for (const auto &producer_op : producer_ops) {
            op_chip[producer_op] = chip;
            bool takes_input_from_queue = true;
            if (args.allow_op_chains && consumer_ops.size() > consumer_to_producer_select_index) {
                const auto &selected = consumer_ops.at(consumer_to_producer_select_index);
                consumer_to_producer_select_index = (consumer_to_producer_select_index + 1) % consumer_ops.size();
                auto &op = netlist.graphs.at(chip).op_map.at(producer_op);
                op.input_names.push_back(selected);
                takes_input_from_queue = false;

                log_assert(op.input_names.size() <= 2, "Op must have a max of 2 inputs");
                if (op.input_names.size() == 2) {
                    convert_unary_op_to_binary(op, chip, netlist);
                }
            } 
            if (takes_input_from_queue) {
                input_ops.insert(producer_op);
            }
            netlist.graphs.at(chip).op_map.at(producer_op).type=rand_unary_op_type();
        }

        // Map those producer ops to `chip_incoming_op_outputs` of the appropriate neighbouring chips - and subtract from the total
        const auto &consumer_chips = identify_consumer_chips_for_producer_ops(chip, eth_connections, chip_incoming_op_outputs.at(chip));
        log_debug(tt::LogRouter, "\tsending to chips:");
        log_assert(consumer_chips.size() == producer_ops.size(), "number of consumer chips must match number of producer ops");
        for (int i = 0; i < consumer_chips.size(); i++) {
            chip_incoming_op_outputs[consumer_chips[i]].push_back({producer_ops[i], chip});
            chip_eth_links_available[consumer_chips[i]] -= 1;
            log_assert(chip_eth_links_available[consumer_chips[i]] >= 0, "Should have at lease 0 eth links available");
            log_debug(tt::LogRouter, "\t\t{} --> {}. Consumer chip has {} eth links remaining", producer_ops[i], consumer_chips[i], chip_eth_links_available[consumer_chips[i]]);
        }

        chip_eth_links_available[chip] -= available_output_eth_links;
        log_assert(chip_eth_links_available[chip] == 0, "All eth links should be have been used");
        
        std::pop_heap(chips_sorted_by_ascending_number_of_ethernet_streams.begin(), chips_sorted_by_ascending_number_of_ethernet_streams.end(), eth_link_count_compare);
        chips_sorted_by_ascending_number_of_ethernet_streams.pop_back();
    }

    for (const auto &[chip, available_eth_links] : chip_eth_links_available) {
        log_assert(available_eth_links == 0, "All eth links should be have been used");
    }

    log_assert(input_ops.size() > 0, "Should have at lease 1 input op");
    log_assert(output_ops.size() > 0, "Should have at lease 1 output op");

    const auto &[input_queues, output_queues] = generate_queue_data(chips_vec, netlist, std::vector<std::string>(input_ops.begin(),input_ops.end()), std::vector<std::string>(output_ops.begin(),output_ops.end()), op_chip, args.input_count);

    return {netlist, input_queues, output_queues};
}


int main(int argc, char **argv) {
    const auto &args = parse_args(std::vector<std::string>(argv, argv + argc));
    int input_count = 1;
    const auto cluster_desc_uniq = tt_ClusterDescriptor::create_from_yaml(args.cluster_desc_path);
    for (int i = 0; i < 4; i++) {
        const auto &[netlist, input_queues, output_queues] = generate_netlist_from_cluster_desc(*cluster_desc_uniq.get(), args);

        const auto &netlist_path = args.output_file_path + "/" + std::to_string(rand()) + ".yaml";
        export_netlist_to_yaml(netlist, netlist_path);

        std::string invocation_command = generate_invocation_command(input_queues, output_queues, netlist_path);
        std::cout << "RUN COMMAND: " << invocation_command << std::endl;
    }
}