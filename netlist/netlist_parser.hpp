// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once


#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "netlist_info_types.hpp"
#include "yaml-cpp/yaml.h"

using namespace std;

class tt_tile_map {
    int rt = 0;
    int ct = 0;
};

using temporal_graph_id_t = int;
//! Netlist parser parses netlist specification and populates structures of netlist_infos
class netlist_parser {
   public:
    tt_device_info device_info;
    unordered_map<string, tt_queue_info> queue_map;
    unordered_map<string, tt_graph_info> graph_map;
    unordered_map<string, tt_program_info> program_map;
    // One entry for each op-fused_op pair in netlist
    // as one fused_op can be referenced by multipel ops
    // Key is "base_op_name_fused_op_id"
    unordered_map<string, tt_fused_op_info> fused_ops_op_map;
    vector<string> program_order;
    vector<string> graph_order;

    unordered_map<string, vector<int>> queue_to_devices_map;
    unordered_map<string, vector<int>> graph_to_devices_map;

    void parse_file(string file = "default_netlist.yaml");
    void parse_string(string netlist);
    void parse_yaml(const YAML::Node& netlist);

    int get_number_of_temporal_graphs() const { return this->temporal_graph_graphs.size(); }
    const std::unordered_set<std::string> &get_graphs_of_temporal_graph(temporal_graph_id_t temporal_graph) const;
    temporal_graph_id_t get_temporal_graph_of_graph(const std::string &graph_name) const;
    std::unordered_map<std::string, tt_dim_info> get_map_of_dim_info_per_input_for_op(const string& op_name);
    static bool is_queue_fed_by_op(tt_queue_info queue);

    void clear();
    void verify_queues();
    void verify_graphs();
    void verify_programs();
    void verify_fused_ops();
    void verify_complex_settings();

    ~netlist_parser() { clear(); }

    struct validation {
        static void validate_splice_op_attributes(tt_op_info const& op_info, int input_index);
    };

   protected:
    std::vector<std::unordered_set<std::string>> temporal_graph_graphs;
    std::unordered_map<std::string, temporal_graph_id_t> graph_temporal_graphs;

    std::unordered_map<std::string, std::string> op_graph_map;
    std::unordered_map<std::string, std::unordered_set<std::string>> producer_consumer_map;

    // One entry for declaration of fused op in netlist
    // Key is "fused_op_id"
    unordered_map<string, tt_fused_op_info> fused_ops_unique_map;

    bool initialized = false;
    //! User should not call the following parse functions manually.  These are helpers for parsing the full netlist
    void parse_devices(const YAML::Node& devices, tt_device_info &device_info);
    void parse_queues(const YAML::Node& queues, unordered_map<string, tt_queue_info> &queue_map);
    void parse_op(const YAML::Node& op, tt_op_info &new_op_info);
    void parse_graphs(const YAML::Node& graphs, unordered_map<string, tt_graph_info> &graph_map);
    void parse_queue_settings(const YAML::Node& queue_settings, tt_instruction_info &instrn);
    void parse_tm_info(
        const YAML::Node &tms,
        const string &op_name,
        const string &op_type,
        const int &relative_input_index,
        bool &transpose,
        tt_tm_info &tm_info,
        std::vector<tt_input_pad_info> &input_padding,
        const bool fused_op_mode,
        const bool binary_int32_add = false);
    void parse_queue_tm_info(
        YAML::Node tms,
        const string &queue_name,
        bool &transpose,
        tt_tm_info &tm_info, 
        std::vector<tt_input_pad_info> &input_padding,
        const bool fused_op_mode);
    int get_tm_index(const string &tm_key, const string &op_name, const string &op_type, const int &num_inputs);
    void parse_tm_ops(const YAML::Node& tms, const string &tm_key, tt_op_info &op_info);
    void parse_queue_tm_ops(const YAML::Node& tms, const string &tm_key, tt_queue_info &queue_info);
    void parse_padding(const YAML::Node& input_padding_node, const string &input_key, tt_op_info &op_info);
    void parse_unpadding(const YAML::Node& input_unpadding_node, const string &input_key, tt_op_info &op_info);
    void parse_execute_instruction(const YAML::Node& execute_instruction, tt_instruction_info &instrn);
    void parse_program(const YAML::Node& program, tt_program_info &program_info);
    void parse_programs(const YAML::Node& programs, unordered_map<string, tt_program_info> &program_map);
    void parse_scheduled_op_fields(const YAML::Node& op_fields, const string& op_name, tt_scheduled_op_info &scheduled_op_info);
    void parse_fused_ops(const YAML::Node& fused_ops);
    void merge_empty_graphs_to_temporal_graphs();
    void merge_non_overlapping_adjacent_temporal_epochs();
    void derive_complex_settings();
    void derive_input_dims_for_op(
        tt_op_info &op_info, 
        const std::unordered_map<std::string, std::pair<tt_dim_info, tt::tt_grid_shape>>& op_dims);
    void derive_input_dims_for_ops();
    void derive_vector_mode_for_ops();
    void derive_input_tile_dims_for_ops();
    void derive_temporal_graphs();
    void expand_multi_instance_structures();
    void compress_temporal_graph_ids();
    void verify_ublock_fits_into_dest(const string& op_name, const tt_dim_info& op_output_dim, DataFormat op_dest_accumulate_data_format, bool full_sync_mode, tt::ARCH arch);
};
