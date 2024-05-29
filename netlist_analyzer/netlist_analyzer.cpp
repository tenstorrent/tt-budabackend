// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_analyzer.hpp"

#include "netlist/netlist_info_types.hpp"

#include "netlist/netlist_utils.hpp"

#include "opmodel.hpp"
#include "perf_lib/op_model/op_model.hpp"

#include <yaml-cpp/yaml.h>

tt_netlist_analyzer::tt_netlist_analyzer(const std::string& arch, const std::string& netlist_path) {
    this->arch = arch;
    m_netlist_parser.parse_file(netlist_path);
};

tt_netlist_analyzer::tt_netlist_analyzer(const std::string &arch, const std::string &netlist_path,
                                         const std::string &cluster_desc_path) {
    this->arch = arch;
    m_netlist_parser.parse_file(netlist_path);
    if (arch == "wormhole_b0" && !cluster_desc_path.empty()) {
        parse_cluster_desc(cluster_desc_path);
    } else if (arch == "grayskull") {
        // TODO add harvesting mask lookup
        m_chips.emplace_back(arch);
    }
}

void tt_netlist_analyzer::parse_cluster_desc(const std::string &cluster_desc_path) {
    std::ifstream fdesc(cluster_desc_path);
    if (fdesc.fail()) {
        throw std::runtime_error("Error: cluster descriptor file " + cluster_desc_path + " does not exist!");
    }
    fdesc.close();

    int chip_id = 0;
    YAML::Node yaml = YAML::LoadFile(cluster_desc_path);
    if (yaml["harvesting"]) {
        for (const auto& chip_node : yaml["harvesting"].as<std::map<int, YAML::Node>>()) {
            chip_id_t chip_id_from_yaml = chip_node.first;
            auto harvesting_info = chip_node.second;
            if (!harvesting_info["noc_translation"].as<bool>()) {
                // TODO warning?
                continue;
            }
            log_assert(chip_id == chip_id_from_yaml, "Chip ids are expected to be consecutive");
            m_chips.emplace_back(this->arch, harvesting_info["harvest_mask"].as<std::uint32_t>());
            chip_id++;
        }
    }
}

void tt_netlist_analyzer::configure_analyzer_for_epoch(const int& epoch_id) {
    m_chips_per_epoch.emplace(epoch_id, std::unordered_set<int>{});
    m_inputs_used_per_epoch.emplace(epoch_id, std::unordered_set<std::string>{});
    m_ops_used_per_epoch.emplace(epoch_id, std::unordered_set<std::string>{});
    m_analyzer_per_epoch.emplace(epoch_id, dpnra::Analyzer(this->arch, m_chips));
    m_queue_settings_used_per_epoch.emplace(epoch_id, std::unordered_map<string, std::pair<bool, bool>>{});
}

void tt_netlist_analyzer::assign_grids_and_edges_for_epoch(const int& epoch_id) {
    log_assert(
        m_analyzer_per_epoch.find(epoch_id) != m_analyzer_per_epoch.end(),
        "Need to configure analyzer for epoch_id={} first before assign_grids_and_edges_for_epoch",
        epoch_id);
    log_assert(
        m_chips_per_epoch.find(epoch_id) != m_chips_per_epoch.end(),
        "Need to configure chips for epoch for epoch_id={} first before assign_grids_and_edges_for_epoch",
        epoch_id);

    const auto& graph_names = m_netlist_parser.get_graphs_of_temporal_graph(epoch_id);
    for (const auto& graph_name : graph_names) {
        const auto& graph = m_netlist_parser.graph_map.at(graph_name);
        for (const auto& op_iter : graph.op_map) {
            // Add op grid
            m_analyzer_per_epoch.at(epoch_id).assign_grid(
                {.name = op_iter.second.name,
                 .type = analyzer::GridType::Op,
                 .op_type = op_iter.second.type, //analyzer::get_op_type(op_iter.second.type),
                 .loc =
                     {
                         .y = op_iter.second.grid_loc.at(0),
                         .x = op_iter.second.grid_loc.at(1),
                     },
                 .shape =
                     {
                         .y = static_cast<int>(op_iter.second.grid_size.r),
                         .x = static_cast<int>(op_iter.second.grid_size.c),
                     },
                 .estimated_cycles = get_estimated_op_cycles(op_iter.second),
                 .grid_transpose = op_iter.second.grid_transpose,
                },
                graph.target_device);
            m_ops_used_per_epoch.at(epoch_id).insert(op_iter.second.name);
            // Add edge for all inputs
            for (const auto& input_name : op_iter.second.input_names) {
                m_analyzer_per_epoch.at(epoch_id).assign_edge(
                    {
                        .name = input_name + "_to_" + op_iter.second.name,
                        .input = input_name,
                        .output = op_iter.second.name,
                    },
                    graph.target_device);
                m_inputs_used_per_epoch.at(epoch_id).insert(input_name);
            }
            m_chips_per_epoch.at(epoch_id).insert(graph.target_device);
        }
    }
    for (const auto& queue_iter : m_netlist_parser.queue_map) {
        if ((m_inputs_used_per_epoch.at(epoch_id).find(queue_iter.second.name) ==
             m_inputs_used_per_epoch.at(epoch_id).end()) and
            (m_ops_used_per_epoch.at(epoch_id).find(queue_iter.second.input) ==
             m_ops_used_per_epoch.at(epoch_id).end())) {
            continue;  // queue isn't an input in this epoch, and not used as an output
        }
        m_queue_settings_used_per_epoch.at(epoch_id).emplace(
            queue_iter.second.name, 
            determine_queue_setting_for_queue_and_epoch(
                queue_iter.second.name, 
                epoch_id
            ));
        std::vector<int> dram_channels = {};
        if(queue_iter.second.loc == QUEUE_LOCATION::DRAM) {
            for (const auto& alloc_info : queue_iter.second.alloc_info) {
                dram_channels.push_back(alloc_info.channel);
            }
        }
        else if (queue_iter.second.loc == QUEUE_LOCATION::HOST) {
            //log_info(tt::LogAnalyzer, "FOUND HOST QUEUE");
            dram_channels.push_back(255); // TODO: double check that 255 is sentinel for host on pipegen spec as well 
        }
        else {
            log_assert(queue_iter.second.loc != QUEUE_LOCATION::INVALID, "Invalid Queue Location");
        }
        // Add queue grid
        m_analyzer_per_epoch.at(epoch_id).assign_grid(
            {
                .name = queue_iter.second.name,
                .type = analyzer::GridType::Queue,
                .shape =
                    {
                        .y = static_cast<int>(queue_iter.second.grid_size.r),
                        .x = static_cast<int>(queue_iter.second.grid_size.c),
                    },
                .dram_channels = std::vector<int>(dram_channels.begin(), dram_channels.end()),
            },
            queue_iter.second.target_device);
        if (queue_iter.second.input != "HOST") {
            m_analyzer_per_epoch.at(epoch_id).assign_edge(
                {
                    .name = queue_iter.second.input + "_to_" + queue_iter.second.name,
                    .input = queue_iter.second.input,
                    .output = queue_iter.second.name,
                },
                queue_iter.second.target_device);
        }
        m_chips_per_epoch.at(epoch_id).insert(queue_iter.second.target_device);
    }
}

void tt_netlist_analyzer::place_and_route_for_epoch(const int& epoch_id) {
    log_assert(
        m_analyzer_per_epoch.find(epoch_id) != m_analyzer_per_epoch.end(),
        "Need to configure analyzer for epoch_id={} first before place_and_route_for_epoch",
        epoch_id);
    log_assert(
        m_chips_per_epoch.find(epoch_id) != m_chips_per_epoch.end(),
        "Need to configure chips for epoch for epoch_id={} first before place_and_route_for_epoch",
        epoch_id);
    for (const auto& chip : m_chips_per_epoch.at(epoch_id)) {
        m_analyzer_per_epoch.at(epoch_id).place_chip(chip);
        m_analyzer_per_epoch.at(epoch_id).route_chip(chip);
    }
}

void tt_netlist_analyzer::place_for_epoch(const int& epoch_id) {
    log_assert(
        m_analyzer_per_epoch.find(epoch_id) != m_analyzer_per_epoch.end(),
        "Need to configure analyzer for epoch_id={} first before place_for_epoch",
        epoch_id);
    log_assert(
        m_chips_per_epoch.find(epoch_id) != m_chips_per_epoch.end(),
        "Need to configure chips for epoch for epoch_id={} first before place_for_epoch",
        epoch_id);
    for (const auto& chip : m_chips_per_epoch.at(epoch_id)) {
        m_analyzer_per_epoch.at(epoch_id).place_chip(chip);
    }
}

[[deprecated]]
void tt_netlist_analyzer::route_for_epoch(const int& epoch_id) {
    log_assert(
        m_analyzer_per_epoch.find(epoch_id) != m_analyzer_per_epoch.end(),
        "Need to configure analyzer for epoch_id={} first before route_for_epoch",
        epoch_id);
    log_assert(
        m_chips_per_epoch.find(epoch_id) != m_chips_per_epoch.end(),
        "Need to configure chips for epoch for epoch_id={} first before route_for_epoch",
        epoch_id);
    for (const auto& chip : m_chips_per_epoch.at(epoch_id)) {
        m_analyzer_per_epoch.at(epoch_id).route_chip(chip);
    }
}

void tt_netlist_analyzer::load_pipes_for_epoch(const int& epoch_id, const string &build_dir_path) {
    log_assert(
        m_analyzer_per_epoch.find(epoch_id) != m_analyzer_per_epoch.end(),
        "Need to configure analyzer for epoch_id={} first before load_pipes_for_epoch",
        epoch_id);
    log_assert(
        m_chips_per_epoch.find(epoch_id) != m_chips_per_epoch.end(),
        "Need to configure chips for epoch for epoch_id={} first before load_pipes_for_epoch",
        epoch_id);
    const string pipegen_yaml_path = build_dir_path + "/temporal_epoch_" + std::to_string(epoch_id) + "/overlay/pipegen.yaml";
    m_analyzer_per_epoch.at(epoch_id).load_pipes_for_chips(pipegen_yaml_path);
}

std::pair<bool, bool> tt_netlist_analyzer::determine_queue_setting_for_queue_and_epoch(const string queue_name, const int& epoch_id) {
    return {false, false};
}

void tt_netlist_analyzer::run_analyzer_checks_for_epoch(const int& epoch_id) {
    log_assert(
        m_analyzer_per_epoch.find(epoch_id) != m_analyzer_per_epoch.end(),
        "Need to configure analyzer for epoch_id={} first before run_analyzer_checks_for_epoch",
        epoch_id);
    log_assert(
        m_chips_per_epoch.find(epoch_id) != m_chips_per_epoch.end(),
        "Need to configure chips for epoch for epoch_id={} first before run_analyzer_checks_for_epoch",
        epoch_id);
    for (size_t chip_id = 0; chip_id < m_chips.size(); chip_id++) {
        if (m_chips_per_epoch.find(chip_id) == m_chips_per_epoch.end()) {
            continue;
        }
    //for (const auto& chip : m_chips_per_epoch.at(epoch_id)) {
        m_analyzer_per_epoch.at(epoch_id).analyze_chip(chip_id);
    }
}

[[deprecated]]
void tt_netlist_analyzer::run_default_flow(){
    for (int e = 0; e < m_netlist_parser.get_number_of_temporal_graphs(); e++) {
        log_info (
            tt::LogAnalyzer,
            "Creating analyzer for temporal_graph/epoch={}", 
            e
        );
        configure_analyzer_for_epoch(e);
        assign_grids_and_edges_for_epoch(e);
        place_and_route_for_epoch(e);
        run_analyzer_checks_for_epoch(e);
    }
}

void tt_netlist_analyzer::run_net2pipe_flow(const string &build_dir_path){
    log_info(tt::LogAnalyzer, "Running analyzer net2pipe flow");

    for (int e = 0; e < m_netlist_parser.get_number_of_temporal_graphs(); e++) {
        log_info (
            tt::LogAnalyzer,
            "Creating analyzer for temporal_graph/epoch={}", 
            e
        );
        configure_analyzer_for_epoch(e);
        assign_grids_and_edges_for_epoch(e);
        place_for_epoch(e);
        load_pipes_for_epoch(e, build_dir_path);
        run_analyzer_checks_for_epoch(e);
        //for (const auto& chip_id : m_chips_per_epoch.at(e)) {
        for (size_t chip_id = 0; chip_id < m_chips.size(); chip_id++) {
            if (m_chips_per_epoch.at(e).find(chip_id) == m_chips_per_epoch.at(e).end()) {
                log_info(tt::LogAnalyzer, "Skipping chip {} in epoch {}", chip_id, e);
                continue;
            }
            const string analyzer_yaml_path = build_dir_path + "/netlist_analyzer/analyzer_output_temporal_epoch_" + std::to_string(e) + "_chip_" + std::to_string(chip_id) + ".yaml";
            log_info(tt::LogAnalyzer, "Exporting data: {}", analyzer_yaml_path);
            m_analyzer_per_epoch.at(e).serialize_chip(chip_id, analyzer_yaml_path);
        }
    }
}

int tt_netlist_analyzer::get_estimated_op_cycles(const tt_op_info& op) const {

    if (op.type == "fused_op") {
        int total_cycles = 0;
        const auto fused_op_list = netlist_utils::get_list_of_tt_op_info_from_fused_op(op, m_netlist_parser.fused_ops_op_map);
        for (const auto & op: fused_op_list) {
            total_cycles += get_estimated_op_cycles(op);
        }
        return total_cycles;
    }

    std::string op_attr = "";
    if(op.type == "reduce") {
        if(op.attributes.reduce_dim == Dim::Z) {
            op_attr = "z";
        }
        else if(op.attributes.reduce_dim == Dim::R) {
            op_attr = "r";
        }
        else if(op.attributes.reduce_dim == Dim::C) {
            op_attr = "c";
        }
    }

    std::string op_type = "";
    if(op.type == "datacopy") {
        op_type = "nop";
    }
    else if(op.type == "lrelu") {
        op_type = "sigmoid";
    }
    else {
        op_type = op.type;
    }

    tt::tt_op_model_desc op_desc = {
        
        .type = op_type,
        .arch = this->arch,

        .data_format = op.output_data_format,
        .math_fidelity = op.math_fidelity,

        // block shape
        .t = static_cast<uint32_t>(op.output_dim.t),
        .mblock_m = static_cast<uint32_t>(op.output_dim.mblock_m),
        .mblock_n = static_cast<uint32_t>(op.output_dim.mblock_n),
        .ublock_rt = static_cast<uint32_t>(op.output_dim.ublock_rt),
        .ublock_ct = static_cast<uint32_t>(op.output_dim.ublock_ct),

        // matmul
        .mblock_k = static_cast<uint32_t>(op.attributes.m_k),
        .ublock_kt = static_cast<uint32_t>(op.attributes.u_kt),
        .sparse_indices = static_cast<uint32_t>(op.attributes.num_sparse_tiles), // TODO: CHECK THIS PARAM

        // op specific attributes and parameters
        .approx_mode = op.attributes.approximate_mode,
        // only for reduce op
        .op_attr = op_attr
    };
    
    return tt::OpModel::get_op_cycles(op_desc);
}

tt_netlist_analyzer::~tt_netlist_analyzer() {
    // TODO: Clean up PNRA
}
