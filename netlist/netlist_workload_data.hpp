// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>
#include <unordered_map>

#include "common/base.hpp"
#include "device/tt_cluster_descriptor.h"
#include "netlist_parser.hpp"
#include "netlist_program.hpp"
#include "tt_digraph.hpp"

namespace tt {
struct tt_address_range {
    uint32_t device = 0;
    uint32_t channel = 0;
    uint64_t base_byte_address = 0;
    uint64_t end_byte_address = 0;
    uint64_t byte_size = 0;
};

//! All encompassing workload_data object which has the parser/graphs/program all encapsulated in 1 object
class netlist_workload_data {
   public:
    using instruction_index_t = int;
    using program_name_t = std::string;
    using instruction_id_t = std::pair<program_name_t, instruction_index_t>;

    std::map<string, tt_digraph> graphs = {};
    std::map<string, tt_queue_wrap> queues = {};
    std::map<program_name_t, netlist_program> programs = {};
    std::vector<string> program_order = {};
    std::vector<string> graph_order = {};
    tt_device_info device_info;
    unordered_map<string, unordered_set<string>> op_to_outputs = {};

    std::unordered_map<program_name_t, std::unordered_map<instruction_index_t, std::vector<instruction_index_t>>>
        temporal_graph_instance_instructions;

    netlist_workload_data(){};
    explicit netlist_workload_data(string path_to_netlist, const tt_backend_config& config = {}) : m_config(config) {
        parser.parse_file(path_to_netlist);
        device_info = parser.device_info;
        populate_queues_from_parser(parser);
        populate_graphs_from_parser(parser);
        populate_programs_from_parser(parser);
        populate_queues_info_and_attributes();
        populate_instruction_temporal_graph_mappings(parser);
        populate_op_to_outputs();
        assert_constants_not_modified_in_programs();
    };
    virtual ~netlist_workload_data(){};

    int get_number_of_temporal_graphs() const { return this->parser.get_number_of_temporal_graphs(); }
    const std::unordered_set<std::string>& get_graphs_of_temporal_graph(temporal_graph_id_t temporal_graph) const {
        return this->parser.get_graphs_of_temporal_graph(temporal_graph);
    }
    temporal_graph_id_t get_temporal_graph_of_graph(const std::string& graph_name) const {
        return this->parser.get_temporal_graph_of_graph(graph_name);
    }
    int get_graph_chip(const std::string& graph_name) const {
        return this->parser.graph_map.at(graph_name).target_device;
    }

    const std::vector<instruction_index_t>& get_execute_statements_belonging_to_current_temporal_graph_instance(
        const netlist_program& program) const {
        return temporal_graph_instance_instructions.at(program.get_name()).at(program.get_current_pc());
    }
    std::set<chip_id_t> compute_target_device_ids();
    const std::vector<std::string>& get_programs();
    // Queue query methods
    bool is_e2e_queue(string queue_name);
    bool is_dynamic_queue(string queue_name);
    bool is_static_queue(string queue_name) { return !is_dynamic_queue(queue_name); }
    bool has_tilized_data(string queue_name) const;
    bool has_untilized_data(string queue_name) {
        return !has_tilized_data(queue_name) or (queues.at(queue_name).my_queue_info.layout == IO_LAYOUT::Flat);
    }
    Dim get_ublock_scan(string queue_name);
    void populate_op_to_outputs();

    netlist_parser parser;
   protected:
    virtual void populate_graphs_from_parser(const netlist_parser& parser);
    virtual void populate_programs_from_parser(const netlist_parser& parser);
    virtual void populate_graph(tt_digraph &graph, const netlist_parser& parser);
    virtual void populate_queues_from_parser(const netlist_parser& parser);
    // Queue query structures
    virtual void populate_queues_info_and_attributes();
    virtual void parse_queue_producer_info();
    virtual void parse_queue_consumer_info();
    void parse_e2e_queue_info();
    void parse_dynamic_queue_info();
    void write_map_for_temporal_graph_instance(
        std::string program_name,
        int temporal_graph_id,
        std::map<std::string, int>& graph_name_to_instruction_index_in_instance);
    void ensure_all_graphs_in_temporal_epoch_included(
        int temporal_epoch_id,
        std::map<std::string, int>& visited_graphs,
        std::map<int, std::set<std::string>>& reference_graphs);
    tt_backend_config m_config = {};

    std::vector<tt_address_range> get_allocation_address_ranges_for_queue_info(const tt_queue_info& queue_info);
    std::vector<tt_address_range> get_allocation_address_ranges_for_queue(const string& queue_name);

    void parse_aliased_queue_info();
    bool is_aliased_io(std::string lhs, std::string rhs);
    std::unordered_map<std::string, std::string> io_to_base_io = {};

    std::unordered_set<std::string> e2e_queues;
    std::unordered_set<std::string> dynamic_queues;

   private:
    void assert_constants_not_modified_in_programs();
    void populate_instruction_temporal_graph_mappings(const netlist_parser& parser);
};

  ostream& operator<<(ostream& out, const tt::tt_address_range& t);
}  // namespace tt

namespace std {
template <class T>
inline void hash_combine(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <>
struct hash<tt::tt_address_range> {
    inline size_t operator()(const tt::tt_address_range& x) const {
        // size_t value = your hash computations over x
        size_t value = std::hash<uint32_t>{}(x.device);
        std::hash_combine(value, x.channel);
        std::hash_combine(value, x.base_byte_address);
        std::hash_combine(value, x.end_byte_address);
        std::hash_combine(value, x.byte_size);
        return value;
    }
};
}  // namespace std
