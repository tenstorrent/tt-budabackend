// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <unordered_map>
#include <string>

#include "netlist/netlist_parser.hpp"
#include "netlist/tt_backend.hpp"

#include "analyzer/inc/dpnra.hpp"

//! Netlist Analyzer Device which checks for BW allocation and overlap
class tt_netlist_analyzer : public tt_backend {
    /////////////////////
    //! OFFICIAL_API
    /////////////////////
   public:
    //! Constructor has to take a string
    tt_netlist_analyzer(const std::string &netlist_path);
    tt_netlist_analyzer(const std::string &arch, const std::string &netlist_path); // TODO: This does not fit this official backend api structure
    ~tt_netlist_analyzer();
    //! initialize - Must be called once and only once before any running of programs
    tt::DEVICE_STATUS_CODE initialize() { return tt::DEVICE_STATUS_CODE::RuntimeError; };
    //! finish - Netlist Analyzer doesn't have runtime component
    tt::DEVICE_STATUS_CODE finish() { return tt::DEVICE_STATUS_CODE::RuntimeError; };
    //! Run Program - Netlist Analyzer doesn't have runtime component
    tt::DEVICE_STATUS_CODE run_program(
        const std::string &program_name, const std::map<std::string, std::string> &parameters) {
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    };
    //! wait_for_idle - Netlist Analyzer doesn't have runtime component
    tt::DEVICE_STATUS_CODE wait_for_idle() { return tt::DEVICE_STATUS_CODE::RuntimeError; };
    //! memory_barrier - Netlist Analyzer doesn't have runtime component
    tt::DEVICE_STATUS_CODE memory_barrier(tt::MemBarType barrier_type, chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores = {}) { return tt::DEVICE_STATUS_CODE::RuntimeError; }
    //! Queue Queries
    const tt::tt_dram_io_desc get_queue_descriptor(const std::string &queue_name) { return {}; };
    const std::vector<std::string>& get_programs() { 
        static std::vector<std::string> program_vector = {};
        return program_vector; 
    }
    /////////////////////
    //! END: OFFICIAL_API
    /////////////////////

    // Analyzer flows
    void run_default_flow();
    void run_net2pipe_flow(const string &build_dir_path);

   private:

    std::string arch;

    netlist_parser m_netlist_parser = {};
    // 
    std::unordered_map<int, dpnra::Analyzer> m_analyzer_per_epoch = {};
    std::unordered_map<int, std::unordered_set<std::string>> m_inputs_used_per_epoch = {};
    std::unordered_map<int, std::unordered_set<std::string>> m_ops_used_per_epoch = {};
    std::unordered_map<int, std::unordered_set<int>> m_chips_per_epoch = {};
    std::unordered_map<int, std::unordered_map<string, std::pair<bool, bool>>> m_queue_settings_used_per_epoch = {};
    

    std::pair<bool, bool> determine_queue_setting_for_queue_and_epoch (const string queue_name, const int& epoch_id);
    void configure_analyzer_for_epoch (const int& epoch_id);
    void assign_grids_and_edges_for_epoch(const int& epoch_id);
    void place_and_route_for_epoch (const int& epoch_id);
    void place_for_epoch (const int& epoch_id);
    void route_for_epoch (const int& epoch_id);
    void load_pipes_for_epoch (const int& epoch_id, const string &build_dir_path);
    void run_analyzer_checks_for_epoch(const int& epoch_id);

    //XXX: Temp Model functions to be moved
    int get_estimated_op_cycles(const tt_op_info& op) const;
};
