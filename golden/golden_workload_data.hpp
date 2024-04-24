// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <string>

#include "common/base.hpp"
#include "model/buffer.hpp"
#include "golden_config.hpp"
#include "golden_digraph.hpp"
#include "golden_queue_setting.hpp"
#include "common/mem_lib.hpp"
#include "netlist/netlist_parser.hpp"
#include "netlist/netlist_program.hpp"
#include "netlist/netlist_workload_data.hpp"


namespace tt::golden {
//! All encompassing workload_data object which has the parser/graphs/program all encapsulated in 1 object
class golden_workload_data : public netlist_workload_data {
   public:
    std::map<string, golden_digraph> graphs = {};
    ~golden_workload_data() {
        for (auto& memptr : allocated_untilized_memories) {
            log_trace(tt::LogGolden, "GC -- Deallocating {}", reinterpret_cast<intptr_t>(memptr));
            deallocate_untilized_memory(memptr);
        }
    };
    golden_workload_data(const string& path_to_netlist, const tt_golden_config& config = {}) : m_config(config) {
        parser.parse_file(path_to_netlist);
        device_info = parser.device_info;
        populate_queues_from_parser(parser);
        populate_graphs_from_parser(parser);
        populate_programs_from_parser(parser);
        populate_queues_info_and_attributes();
    };
    void reset_queues();

    // API functions for temporal graphs
    bool set_and_check_execution_status_of_graph(const std::string& graph_name);
    void save_queue_setting_for_temporal_graph(
        const std::string& graph_name, const tt::golden::golden_queue_setting& queue_setting);
    std::unordered_map<std::string, tt::golden::golden_queue_setting> get_queue_settings_for_temporal_graph(
        const std::string& graph_name);
    std::string get_temporal_graph(const std::string& graph_name);
    void clear_all_temporal_graph_execution_status();
    void clear_temporal_graph_execution_status(const std::string& graph_name);

    //! Queue allocation and deallocation API (mainly for checking atm)
    void allocate_queue(const std::string& queue_name);
    void deallocate_queue(const std::string& queue_name);
    void allocate_static_queues();
    bool not_allocated(const std::string& queue_name);
    //! Dynamic memory management
    std::shared_ptr<std::vector<uint32_t>> allocate_untilized_memory(std::string q_name, int num_entries);
    void deallocate_untilized_memory(void* ptr);

   private:
    std::vector<void*> allocated_untilized_memories;
    void populate_temporal_graphs_from_parser(const netlist_parser& parser);
    void populate_graphs_from_parser(const netlist_parser& parser);
    void populate_graph(const tt_graph_info& graph_info, const ARCH arch, const netlist_parser& parser);
    void populate_queues_from_parser(const netlist_parser& parser);
    
    void run_instruction(netlist_program& program);
    tt_golden_config m_config = {};

    // Helper functions for temporal graphs
    bool is_subgraph(const std::string& graph_name);
    void set_execution_status_if_subgraph(const std::string& graph_name);
    bool is_last_graph_to_execute(const std::string& graph_name);
    std::unordered_map<std::string, std::string> map_of_subgraphs_to_temporal_graph = {};
    std::unordered_map<std::string, bool> map_of_subgraphs_to_execution_status = {};
    std::unordered_map<std::string, std::vector<std::string>> map_temporal_graph_list_of_subgraphs = {};
    std::unordered_map<std::string, std::unordered_map<std::string, tt::golden::golden_queue_setting>>
        saved_temporal_graph_map_of_queue_settings = {};
    // Helpers for allocation/deallocation API
    std::unordered_map<std::string, std::vector<tt_address_range>> allocated_queues = {};
    bool is_address_range_overlapping(tt_address_range a1, tt_address_range a2);
    bool is_address_same(tt_address_range a1, tt_address_range a2);

    virtual void populate_queues_info_and_attributes();
    virtual void parse_queue_producer_info();
    virtual void parse_queue_consumer_info();
};
}
