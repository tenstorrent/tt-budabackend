// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "golden_config.hpp"
#include "golden_queue_setting.hpp"
#include "golden_workload_data.hpp"
#include "netlist/netlist_program.hpp"
#include "netlist/tt_backend.hpp"

namespace tt::golden {
//! Golden Device which defines the spec that all devices should follow
class tt_golden : public tt_backend {
    /////////////////////
    //! OFFICIAL_API
    /////////////////////
   public:
    //! Constructor has to take a string
    tt_golden(const std::string &netlist_path);
    tt_golden(const std::string &netlist_path, const tt_golden_config &config);
    ~tt_golden();
    //! initialize - Must be called once and only once before any running of programs
    tt::DEVICE_STATUS_CODE initialize();
    //! finish - Must be called once and only once after all programs are run.  Clean up
    tt::DEVICE_STATUS_CODE finish();
    //! Run Program - Must be done to run program specified
    tt::DEVICE_STATUS_CODE run_program(const string &program_name, const std::map<string, string> &parameters);
    //! wait_for_idle - Optionally called to block until all launched programs are complete
    tt::DEVICE_STATUS_CODE wait_for_idle() { return tt::DEVICE_STATUS_CODE::Success; };
    tt::DEVICE_STATUS_CODE memory_barrier(tt::MemBarType barrier_type, chip_id_t chip, const std::unordered_set<tt_xy_pair>& cores = {}) { return tt::DEVICE_STATUS_CODE::Success; }
    //! Queue Queries
    const std::map<string, tt_queue_info> get_host_queue_info_map(bool append_rams = false);
    const std::map<string, tt_queue_info> get_host_ram_info_map();
    const std::map<string, tt_queue_info> get_device_queue_info_map();
    const tt_queue_info get_queue_info(const std::string &queue_name);
    const tt::tt_dram_io_desc get_queue_descriptor(const std::string &queue_name);

    // FIXME -- remove once all directed tests use sw untilization/tilization path.
    //! Debug Interface Functions for queues -- Used by backend tests -- Will be REMOVED once the other paths are
    //! supported
    void push_input(const tt_queue_info &q_info, std::shared_ptr<tt_tensor> input, const int ptr = -1);
    std::shared_ptr<tt_tensor> pop_output(const tt_queue_info &q_info);
    std::shared_ptr<tt_tensor> get_output(const tt_queue_info &q_info, const int ptr = -1);

   protected:
    //! Program Execution Callbacks for program
    void execute_instruction_callback(netlist_program &program);
    void pre_instrn_instruction_callback(netlist_program &program);
    void post_instrn_instruction_callback(netlist_program &program);
    void run_instruction(netlist_program &program) {
        // Run instruction with an execution callback which gets called whenever an execute command happens
        program.run_instruction_with_callbacks(
            [this](netlist_program &program) { this->pre_instrn_instruction_callback(program); },
            [this](netlist_program &program) { this->execute_instruction_callback(program); },
            [this](netlist_program &program) { this->post_instrn_instruction_callback(program); });
    };
    /////////////////////
    //! END: OFFICIAL_API
    /////////////////////
   public:
    void reset_queues();
    void run_programs();

   private:
    std::string m_netlist_path = "";
    tt_golden_config m_config = {};
    golden_workload_data m_workload;  // structure which has all the graphs/programs and state
    std::mutex get_queue_descriptor_mutex;
    golden_workload_data &get_workload(std::string netlist_path);

    void propagate_variable(netlist_program &program, string variable_string);
    void propagate_variable_for_io(
        const tt::golden::golden_queue_setting &queue_setting_snapshot, string variable_string, string io_name);
    std::unordered_map<std::string, tt_queue_setting_info> executed_queue_settings = {};
    std::unordered_map<string, tt::tt_dram_io_desc> queue_descriptor_cache = {};
    void load_parameter_and_constant_queues();
};
}
