// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include "perf_base.hpp"
#include "common/buda_soc_descriptor.h"
#include "perf_descriptor.hpp"

namespace postprocess {

extern std::recursive_mutex perf_state_mutex;

using perf::instruction_info_wrapper;
using perf::tt_perf_digraph_wrapper;
using perf::PostprocessModelDesc;
using perf::core_descriptor;
using perf::tt_perf_device_alignment;

// Records the runtime state that performance infra is sensitive to.
// This struct needs to be THREAD SAFE
// Multiple threads might be using an object with this type
// Any variable added here must have two apis one for updating it and one for reading it
// and BOTH have lock a mutex first before modifying or returning the variable
struct PerfState {

private:
    tt::TargetDevice target_device_type;
    perf::PerfDesc perf_desc;
    // vector of all the instructions that are executed so far
    // Only the pointer to the instruction will be stored plus some information on program/graph id
    std::vector<instruction_info_wrapper> executed_instr;
    // All graphs in every program
    unordered_map<uint, uint> program_id_to_num_instructions_executed;
    // Program name to all instructions within that program
    // Will be updated for each program in initialize_for_program api
    unordered_map<string, std::vector<tt_instruction_info> > program_name_to_instructions;
    std::unordered_map<string, std::shared_ptr<tt_perf_digraph_wrapper>> graph_wrappers;
    // Chip id to soc descriptor and aiclk
    std::unordered_map<uint, buda_SocDescriptor> chip_id_to_sdesc;
    std::unordered_map<uint, uint> chip_id_to_aiclk;
    // Op name to the set of output nodes (ops or queues)
    std::unordered_map<string, unordered_set<string>> op_to_outputs;
    // Queue name to queue wrapper
    std::unordered_map<string, tt_queue_wrap> queues;
    // Op name to performance model descriptor
    std::unordered_map<string, PostprocessModelDesc> op_to_perf_model_desc;
    int total_input_count = -1;
    uint num_instructions_processed = 0;

    // Following variables will be used in push_instruction api to calculate the local/global epoch index
    uint global_epoch_id = 0;
    uint local_epoch_id = 0;
    int program_id = -1;
    string last_program_name = "";

    // A data structure for storing the epoch performance info. Which is queried by runtime
    vector<EpochPerfInfo> all_epochs_perf_info;

    bool postprocessor_executed = false;
    // Following variables will be used for device <-> host alignment
    tt_perf_device_alignment device_alignment_info;
    unordered_map<tt_xy_pair, vector<uint64_t>> last_epoch_kernel_runtime;
    string test_output_dir;
    bool perf_check_passed = true;
    // Using the following two parameters when overlay-decoupling is enabled
    // This is part of the mechanism to ensure all cores will start at the same time when overlay-decouple is enabled
    // After receiving epoch-command, all cores will wait on a mailbox to increment before executing the epoch
    // This mailbox gets updated after host sends the epoch commands for that epoch to all cores
    std::unordered_map<uint, tt_xy_pair> device_to_first_core;
    std::unordered_map<uint, uint> device_to_epoch_id;
public:


    PerfState(): target_device_type() {}
    PerfState(const string &test_output_dir, const perf::PerfDesc &perf_desc, const tt::TargetDevice &device_type): test_output_dir(test_output_dir), perf_desc(perf_desc), target_device_type(device_type) {
        log_debug(tt::LogPerfInfra, "Running device_perf_light constructor");
    }

    void initialize_for_program(const netlist_program &program,
                            const std::unordered_map<chip_id_t, buda_SocDescriptor> &sdesc_per_chip,
                            const std::map<string, tt_digraph> &graphs,
                            const unordered_map<string, unordered_set<string>> &op_to_outputs,
                            const std::map<string, tt_queue_wrap> &queues);
    
    ////////////////////////////////////////////////////////////////////////////////
    // apis for updating device perf state
    // We need to lock perf_state_mutex before updating
    ////////////////////////////////////////////////////////////////////////////////
    void update_executed_instr(string program_name, int pc);
    void update_graph_wrappers(const string &graph_name, const tt_digraph &graph);
    void update_program_name_to_instruction(const netlist_program &program);
    void update_chip_id_to_sdesc(const uint &chip_id, const buda_SocDescriptor &soc_descriptor);
    void update_chip_id_to_aiclk(const uint &chip_id, const uint &aiclk);
    void update_op_to_outputs(const string &op_name, const unordered_set<string> &outputs);
    void set_perf_desc(const perf::PerfDesc &new_perf_desc);
    void update_total_input_count(int num_inputs);
    void update_queues(const string &queue_name, const tt_queue_wrap &queue);
    void set_postprocessor_executed();
    void update_all_epochs_info(const vector<EpochPerfInfo> &epoch_perf_info);
    void update_device_alignment_info_start(const int device_id, const uint64_t device_start, const uint64_t host_start);
    void update_device_alignment_info_end(const int &device_id, const uint64_t device_end, const uint64_t host_end);
    void update_last_epoch_kernel_runtime(const tt_xy_pair &core_coord, const vector<uint64_t> kernel_runtime);
    void update_test_output_dir(const string &output_dir);
    void update_op_to_model_desc(const tt_digraph &graph);
    void update_num_instructions_processed(const uint new_instructions_processed);
    void update_perf_check(bool check);
    void update_device_to_first_core(uint device_id, const tt_xy_pair &core_coord);
    void update_device_to_epoch_id(uint device_id, uint epoch_id);

    ////////////////////////////////////////////////////////////////////////////////
    // apis for getting device perf state
    // We need to lock perf_state_mutex first
    ////////////////////////////////////////////////////////////////////////////////
    std::pair<uint32_t, const instruction_info_wrapper> get_global_epoch_idx_and_instr_for_core(perf::PerfDumpHeader header) const;
    bool is_physical_core_active_in_graph(const string &graph_name, const tt_xy_pair &core) const;
    const unordered_map<string, core_descriptor> &get_core_to_desc_map(const string &graph_name) const;
    const core_descriptor &get_core_desc(const string &graph_name, const tt_xy_pair &core) const;
    const buda_SocDescriptor &get_sdesc_for_device(const int device_id) const;
    std::shared_ptr<tt_perf_digraph_wrapper> get_graph_wrapper(const string &graph_name) const;
    const std::unordered_map<string, tt_queue_wrap> &get_all_queues() const;
    std::unordered_map<string, tt_digraph> get_all_graphs() const;
    const tt_digraph &get_graph(const string &graph_name) const;
    uint get_aiclk(const uint &target_device) const;
    int get_total_input_count() const;
    const perf::PerfDesc &get_perf_desc() const;
    bool is_postprocessor_executed() const;
    tt_queue_wrap get_queue(const string &queue_name) const;
    bool is_queue(const string &queue_name) const;
    const vector<EpochPerfInfo> &get_all_epochs_perf_info() const;
    const tt_perf_device_alignment &get_device_alignment_info() const;
    unordered_map<tt_xy_pair, vector<uint64_t>> get_last_epoch_kernel_runtime() const;
    uint get_num_instructions_executed(const uint &program_id) const;
    const std::vector<instruction_info_wrapper> &get_executed_instr() const;
    std::vector<instruction_info_wrapper> get_unprocessed_executed_instr() const;
    unordered_set<string> get_outputs_for_op(const string &op_name) const;
    const string &get_test_output_dir() const;
    const std::unordered_map<string, PostprocessModelDesc> &get_all_perf_model_desc() const;
    PostprocessModelDesc get_op_perf_model_desc(const string &op_name) const;
    uint get_num_instructions_processed() const;
    bool get_perf_check() const;
    tt_xy_pair get_first_core_for_device_id(uint device_id) const;
    uint get_epoch_id_for_device_id(uint device_id) const;
    tt::TargetDevice get_target_device_type() const;
};

}