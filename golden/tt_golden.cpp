// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_golden.hpp"

#include "common/cache_lib.hpp"
#include "common/io_lib.hpp"
#include "golden_io.hpp"
#include "golden_utils.hpp"
#include "common/size_lib.hpp"
#include "utils/logger.hpp"
#include "utils/scoped_timer.hpp"

namespace tt::golden {
void tt_golden::propagate_variable_for_io(
    const tt::golden::golden_queue_setting &queue_setting_snapshot, string variable_string, string io_name) {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    std::map<string, tt_queue_wrap> &queues = workload->queues;
    // This is for cases where the rd_ptr gets updated in variables and this needs to propagate back to queues.
    // See Issue: tenstorrent/budabackend#113
    if (executed_queue_settings.find(io_name) != executed_queue_settings.end()) {
        auto &queue_setting = executed_queue_settings.at(io_name);
        if (queues[io_name].my_queue_info.type == IO_TYPE::Queue) {
            if (variable_string == queue_setting.rd_ptr_autoinc) {
                int rd_ptr_autoinc = queue_setting_snapshot.rd_ptr_autoinc;
                queues[io_name].my_io->rd_ptr_autoinc = rd_ptr_autoinc;
            }
            if (variable_string == queue_setting.global_rdptr_autoinc) {
                int global_rdptr_autoinc = queue_setting_snapshot.global_rdptr_autoinc;
                queues[io_name].my_io->global_rdptr_autoinc = global_rdptr_autoinc;
            }
            if (variable_string == queue_setting.rd_ptr_local) {
                int rd_ptr_local = queue_setting_snapshot.rd_ptr_local;
                queues[io_name].my_io->set_local_rd_ptr(rd_ptr_local);
            }
            log_debug(tt::LogGolden, "Checking queue={}::{}", io_name, variable_string);
            if (variable_string == queue_setting.rd_ptr_global) {
                int raw_rd_ptr_global = queue_setting_snapshot.rd_ptr_global;
                int num_entries = queues[io_name].my_queue_info.entries;
                int rd_ptr_global = raw_rd_ptr_global % (2 * num_entries);
                int queue_rd_ptr_global = queues[io_name].my_io->get_global_rd_ptr();
                int queue_wr_ptr_global = queues[io_name].my_io->get_global_wr_ptr();
                log_debug(tt::LogGolden, "Raw {}={}-->{}", variable_string, queue_rd_ptr_global, raw_rd_ptr_global);
                log_debug(tt::LogGolden, "Propagating {}={}-->{}", variable_string, queue_rd_ptr_global, rd_ptr_global);
                queues[io_name].my_io->set_global_rd_ptr(rd_ptr_global);
                bool upper_rd_ptr_global =
                    (raw_rd_ptr_global >= num_entries) and (raw_rd_ptr_global != (2 * num_entries));
                bool upper_queue_rd_ptr_global =
                    (queue_rd_ptr_global >= num_entries) and (queue_rd_ptr_global != (2 * num_entries));
                bool wrapped_rd = (upper_rd_ptr_global xor upper_queue_rd_ptr_global);
                bool leftover_wr = rd_ptr_global < (queue_wr_ptr_global % (2 * num_entries));
                log_debug(
                    tt::LogGolden,
                    "upper_rd_ptr_global:{} -- upper_queue_rd_ptr_global {} -- wrapped_rd {}",
                    upper_rd_ptr_global,
                    upper_queue_rd_ptr_global,
                    wrapped_rd);
                if (wrapped_rd and not leftover_wr) {
                    // Setting the rd_ptr to itself will pop everything.
                    queues[io_name].my_io->backdoor_wrap_global_wr_ptr();
                }
                [[maybe_unused]] int final_queue_wr_ptr_global = queues[io_name].my_io->get_global_wr_ptr();
                [[maybe_unused]] int final_queue_rd_ptr_global = queues[io_name].my_io->get_global_wr_ptr();
                log_debug(tt::LogGolden, "Final wr_ptr {}-->{}", queue_wr_ptr_global, final_queue_wr_ptr_global);
                log_debug(tt::LogGolden, "Final rd_ptr {}-->{}", queue_wr_ptr_global, final_queue_rd_ptr_global);
            }
        } else if (queues[io_name].my_queue_info.type == IO_TYPE::RandomAccess) {  // IO_TYPE::RandomAccess
            if (variable_string == queue_setting.global_wrptr_autoinc) {
                int global_wrptr_autoinc = queue_setting_snapshot.global_wrptr_autoinc;
                queues[io_name].my_io->global_wrptr_autoinc = global_wrptr_autoinc;
            }
            if (variable_string == queue_setting.global_rdptr_autoinc) {
                int global_rdptr_autoinc = queue_setting_snapshot.global_rdptr_autoinc;
                queues[io_name].my_io->global_rdptr_autoinc = global_rdptr_autoinc;
            }
            if (variable_string == queue_setting.wr_ptr_global) {
                int wr_ptr_global = queue_setting_snapshot.wr_ptr_global;
                if (not queue_setting_snapshot.read_only) {
                    queues[io_name].my_io->set_global_wr_ptr(wr_ptr_global);
                }
            }
            if (variable_string == queue_setting.rd_ptr_global) {
                int rd_ptr_global = queue_setting_snapshot.rd_ptr_global;
                queues[io_name].my_io->set_global_rd_ptr(rd_ptr_global);
            }
        }
    }
}
void tt_golden::propagate_variable(netlist_program &program, string variable_string) {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    // This is for cases where the rd_ptr gets updated in variables and this needs to propagate back to queues.
    // See Issue: tenstorrent/budabackend#113
    for (const auto &queue_setting : executed_queue_settings) {
        string io_name = queue_setting.second.name;
        auto queue_setting_snapshot = tt::golden::get_queue_setting_snapshot(
            program, queue_setting.second, workload->queues.at(io_name).my_queue_info);
        propagate_variable_for_io(queue_setting_snapshot, variable_string, io_name);
    }
}
//! Constructor --> Constructs the workload data which is the data/state of the netlist.
//! That should be saved across backends
tt_golden::tt_golden(const std::string &netlist_path) : tt_golden(netlist_path, tt_golden_config{}){};
tt_golden::tt_golden(const std::string &netlist_path, const tt_golden_config &config) :
    m_netlist_path(netlist_path), m_config(config), m_workload(netlist_path, config) {
    // Construct backend object
    // Put objects in the cache for other processes to use
    tt_object_cache<golden_workload_data>::set(m_netlist_path, &m_workload);
    tt_object_cache<tt_golden_config>::set(m_netlist_path, &m_config);
};

tt_golden::~tt_golden() { finish(); }
//! initialize - Must be called once and only once before any running of programs
tt::DEVICE_STATUS_CODE tt_golden::initialize() {
    if (m_config.tti) {
        load_parameter_and_constant_queues();
    }
    return tt::DEVICE_STATUS_CODE::Success;
}
//! finish - Must be called once and only once after all programs are run.  Clean up
tt::DEVICE_STATUS_CODE tt_golden::finish() {
    if (tt_object_cache<golden_workload_data>::exists(m_netlist_path)) {
        tt_object_cache<golden_workload_data>::clear(m_netlist_path);
    }
    if (tt_object_cache<tt_golden_config>::exists(m_netlist_path)) {
        tt_object_cache<tt_golden_config>::clear(m_netlist_path);
    }
    return tt::DEVICE_STATUS_CODE::Success;
}
//! Run Program - Must be done to run program specified
tt::DEVICE_STATUS_CODE tt_golden::run_program(const string &program_name, const std::map<string, string> &parameters) {
    PROFILE_SCOPE_MS();
    try {
        auto workload = tt::golden::io::get_workload(m_netlist_path);
        log_assert(
            workload->programs.find(program_name) != workload->programs.end(),
            "Cannot find program={} in loaded netlist",
            program_name);
        netlist_program &program = workload->programs.at(program_name);
        workload->clear_all_temporal_graph_execution_status();
        executed_queue_settings.clear();
        program.reset();
        program.set_params(parameters);
        while (!program.done()) {
            try {
                run_instruction(program);
            } catch (const std::exception &e) {
                log_error("{}", e.what());
                log_fatal("Error running instruction {}", program.get_current_instruction());
            }
        }
        return tt::DEVICE_STATUS_CODE::Success;
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        return tt::DEVICE_STATUS_CODE::RuntimeError;
    }
}
void tt_golden::run_programs() {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    for (const auto &program_name : workload->program_order) {
        if (run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::RuntimeError) {
            log_fatal("Program {} Failed to execute in Golden", program_name);
        }
    }
}

//! Queue Queries
const std::map<string, tt_queue_info> tt_golden::get_host_queue_info_map(bool append_rams) {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    std::map<string, tt_queue_info> queue_info_map = {};
    for (auto const &q : workload->queues) {
        if ((q.second.my_queue_info.input == "HOST") ||
            ((q.second.my_queue_info.type == IO_TYPE::RandomAccess) && append_rams)) {
            queue_info_map.insert({q.second.my_queue_info.name, q.second.my_queue_info});
        }
    }
    return queue_info_map;
}

const std::map<string, tt_queue_info> tt_golden::get_host_ram_info_map() {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    std::map<string, tt_queue_info> queue_info_map = {};
    for (auto const &q : workload->queues) {
        if (q.second.my_queue_info.type == IO_TYPE::RandomAccess) {
            queue_info_map.insert({q.second.my_queue_info.name, q.second.my_queue_info});
        }
    }
    return queue_info_map;
}

const std::map<string, tt_queue_info> tt_golden::get_device_queue_info_map() {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    std::map<string, tt_queue_info> queue_info_map = {};
    for (auto const &q : workload->queues) {
        if (q.second.my_queue_info.input != "HOST") {
            queue_info_map.insert({q.second.my_queue_info.name, q.second.my_queue_info});
        }
    }
    return queue_info_map;
}
const tt_queue_info tt_golden::get_queue_info(const std::string &queue_name) {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    log_assert(
        workload->queues.find(queue_name) != workload->queues.end(), "get_queue_info cannot find queue={}", queue_name);
    return workload->queues.at(queue_name).my_queue_info;
}

const tt::tt_dram_io_desc tt_golden::get_queue_descriptor(const std::string &queue_name) {
    const std::lock_guard<std::mutex> lock(get_queue_descriptor_mutex);
    // RMW operation on queue_descriptor_cache. Ensure that this is thread safe
    if (queue_descriptor_cache.find(queue_name) == queue_descriptor_cache.end()) {
        auto workload = tt::golden::io::get_workload(m_netlist_path);
        auto config = tt::golden::io::get_config(m_netlist_path);
        log_assert(
            workload->queues.find(queue_name) != workload->queues.end(), "get_queue_desc cannot find queue={}", queue_name);
        auto q_info = workload->queues.at(queue_name).my_queue_info;
        queue_descriptor_cache.insert({queue_name, get_tt_dram_io_desc_from_tt_queue_info(q_info)});
        tt_dram_io_desc& dram_io_desc = queue_descriptor_cache.at(queue_name);
        dram_io_desc.netlist_path = m_netlist_path;
        dram_io_desc.backend_type = tt::DEVICE::Golden;
        dram_io_desc.bufq_start_addr_channel = std::vector<std::pair<uint32_t, uint16_t>>(q_info.grid_size.c * q_info.grid_size.r);
        dram_io_desc.bufq_mapping = std::vector<void *>(q_info.grid_size.c * q_info.grid_size.r);
        bool is_raw_df = dram_io_desc.bufq_target_format == DataFormat::RawUInt8 ||
                        dram_io_desc.bufq_target_format == DataFormat::RawUInt16 ||
                        dram_io_desc.bufq_target_format == DataFormat::RawUInt32;
        if (config->ignore_data_format_precision && !is_raw_df) {
            dram_io_desc.bufq_target_format = DataFormat::Float32;
        }
        if (config -> tti) {
            // Get stride descriptor populated from TTI
            auto prestride_per_io = config -> tti -> get_io_prestride_map();
            if (prestride_per_io.find(queue_name) != prestride_per_io.end()) {
                dram_io_desc.s_descriptor.stride = std::get<3>(prestride_per_io.at(queue_name));
                for (int x = 0; x < dram_io_desc.s_descriptor.stride; x++) {
                    for (int y = 0; y < dram_io_desc.s_descriptor.stride; y++) {
                        dram_io_desc.s_descriptor.xy_offsets.push_back({x, y});
                    }
                }
            }      
        }
    }
    return queue_descriptor_cache.at(queue_name);
}

//! FIXME: DEPRECATED.. Should use static vars --> pybuda API does not use this.
void tt_golden::reset_queues() {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    workload->reset_queues();
}

//! Program Execution Callbacks for program
void tt_golden::execute_instruction_callback(netlist_program &program) {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    std::map<string, tt_queue_wrap> &queues = workload->queues;
    std::map<string, golden_digraph> &graphs = workload->graphs;
    vector<tt_queue_setting_info>::iterator it;
    tt_instruction_info instrn = program.get_current_instruction();
    tt_graph_info graph_info = graphs.at(workload->get_temporal_graph(instrn.graph_name)).my_graph_info;

    // Try to save all queue settings
    for (it = instrn.queue_settings.begin(); it != instrn.queue_settings.end(); ++it) {
        string q_name = it->name;
        auto queue_setting_snapshot =
            tt::golden::get_queue_setting_snapshot(program, *it, queues[q_name].my_queue_info);
        workload->save_queue_setting_for_temporal_graph(instrn.graph_name, queue_setting_snapshot);
    }

    if (not workload->set_and_check_execution_status_of_graph(instrn.graph_name)) {
        return;
    }
    // Process saved Queue Settings
    auto queue_settings_map = workload->get_queue_settings_for_temporal_graph(graph_info.name);
    for (const auto &queue_setting_it : queue_settings_map) {
        string q_name = queue_setting_it.first;
        auto queue_setting_snapshot = queue_setting_it.second;

        if (queue_setting_snapshot.zero and (not queue_setting_snapshot.prolog)) {
            log_fatal(
                "Zero cannot be set to true for a non-prolog IO={} in graph={} in program={}",
                q_name,
                instrn.graph_name,
                program.get_name());
        }
        // Golden check for epilog/prolog/zero for ram with global_wrptr_autoinc - #1104
        bool ram_autoinc = ((queues[q_name].my_queue_info.type == IO_TYPE::RandomAccess) and
                                  queue_setting_snapshot.global_wrptr_autoinc);
        log_assert (
            (not ram_autoinc) or
            ((not queue_setting_snapshot.epilog) and
             (not queue_setting_snapshot.prolog) and
             (not queue_setting_snapshot.zero)),
            "IO setting={} cannot set global_wrptr_autoinc for RandomAccess type with epilog/prolog/zero set",
            queue_setting_snapshot
        );
        bool streaming_queue =
            (not queue_setting_snapshot.prolog) and                // prolog queues don't care about input-count
            (not queue_setting_snapshot.epilog) and                // Epilog does not either
            (not queue_setting_snapshot.global_rdptr_autoinc) and  // If we have a global pop by device, we don't care
                                                                   // about input-count and entries.
            (queue_setting_snapshot.rd_ptr_autoinc) and  // If we have queue which doesn't increment the local rd_ptr,
                                                         // we don't care about input-count and entries.
            (queues[q_name].my_queue_info.type == IO_TYPE::Queue);
        log_assert(
            (not streaming_queue) or (graph_info.input_count <= queues[q_name].my_queue_info.entries),
            "input_count={} for graph={} must be less than or equal to entries={} in streaming queue={}",
            graph_info.input_count,
            graph_info.name,
            queues[q_name].my_queue_info.entries,
            q_name);
        log_assert(
            (not streaming_queue) or ((queues[q_name].my_queue_info.entries % graph_info.input_count) == 0),
            "entries={} for graph={} must be multiples of input-count={} in streaming queue={}",
            queues[q_name].my_queue_info.entries,
            graph_info.name,
            graph_info.input_count,
            q_name);
        queues[q_name].my_io->prolog = queue_setting_snapshot.prolog;
        queues[q_name].my_io->epilog = queue_setting_snapshot.epilog;
        queues[q_name].my_io->set_zero(queue_setting_snapshot.zero);
        // Update Queue Settings
        if (executed_queue_settings.find(q_name) == executed_queue_settings.end()) {
            executed_queue_settings.insert({q_name, queue_setting_snapshot.saved_q_info});
            // Only set the variables if they have been set before for the queue
            if (queues[q_name].my_queue_info.type == IO_TYPE::Queue) {
                queues[q_name].my_io->rd_ptr_autoinc = queue_setting_snapshot.rd_ptr_autoinc;
                queues[q_name].my_io->global_rdptr_autoinc = queue_setting_snapshot.global_rdptr_autoinc;
                queues[q_name].my_io->set_local_rd_ptr(queue_setting_snapshot.rd_ptr_local);
                queues[q_name].my_io->set_global_rd_ptr(queue_setting_snapshot.rd_ptr_global);

            } else {  // IO_TYPE::RandomAccess
                if (queue_setting_snapshot.global_wrptr_autoinc > -1) {
                    queues[q_name].my_io->global_wrptr_autoinc = queue_setting_snapshot.global_wrptr_autoinc;
                } else {
                    queues[q_name].my_io->global_wrptr_autoinc = 0;
                }
                queues[q_name].my_io->rd_ptr_autoinc = 0;
                if (queue_setting_snapshot.global_rdptr_autoinc > -1) {
                    queues[q_name].my_io->global_rdptr_autoinc = queue_setting_snapshot.global_rdptr_autoinc;
                } else {
                    queues[q_name].my_io->global_rdptr_autoinc = 0;
                }
                queues[q_name].my_io->set_global_wr_ptr(queue_setting_snapshot.wr_ptr_global);
                queues[q_name].my_io->set_global_rd_ptr(queue_setting_snapshot.rd_ptr_global);
            }
        } else {
            // Otherwise propagate
            executed_queue_settings[q_name] = queue_setting_snapshot.saved_q_info;
            if (queues[q_name].my_queue_info.type == IO_TYPE::Queue) {
                propagate_variable_for_io(
                    queue_setting_snapshot, queue_setting_snapshot.saved_q_info.rd_ptr_autoinc, q_name);
                propagate_variable_for_io(
                    queue_setting_snapshot, queue_setting_snapshot.saved_q_info.global_rdptr_autoinc, q_name);
                propagate_variable_for_io(
                    queue_setting_snapshot, queue_setting_snapshot.saved_q_info.rd_ptr_local, q_name);
                propagate_variable_for_io(
                    queue_setting_snapshot, queue_setting_snapshot.saved_q_info.rd_ptr_global, q_name);
            } else {  // IO_TYPE::RandomAccess
                if (queue_setting_snapshot.global_wrptr_autoinc > -1) {
                    propagate_variable_for_io(
                        queue_setting_snapshot, queue_setting_snapshot.saved_q_info.global_wrptr_autoinc, q_name);
                }
                propagate_variable_for_io(
                    queue_setting_snapshot, queue_setting_snapshot.saved_q_info.wr_ptr_global, q_name);
                propagate_variable_for_io(
                    queue_setting_snapshot, queue_setting_snapshot.saved_q_info.rd_ptr_global, q_name);
            }
        }
    }

    log_assert(graphs.find(graph_info.name) != graphs.end(), "Golden Backend Cannot find graph={}", graph_info.name);
    log_debug(
        tt::LogGolden,
        "Running Execute Instruction for Graph={}: PC={} run amount: {}",
        graph_info.name,
        program.get_current_pc(),
        graph_info.input_count);

    // Actually execute the graph across input counts.
    for (int i = 0; i < graph_info.input_count; ++i) {
        graphs[graph_info.name].run();
        // Pop queues with global_rdptr_autoinc
        for (const auto &queue_setting_it : queue_settings_map) {
            string q_name = queue_setting_it.first;
            auto queue_setting_snapshot = queue_setting_it.second;
            if (queues[q_name].my_queue_info.type == IO_TYPE::Queue) {
                queues[q_name].my_io->pop_n(queue_setting_snapshot.global_rdptr_autoinc);
            }
        }
    }

    // reset local read ptrs
    for (it = instrn.queue_settings.begin(); it != instrn.queue_settings.end(); ++it) {
        string q_name = it->name;
        bool is_queue = (queues[q_name].my_queue_info.type == IO_TYPE::Queue);
        if (is_queue) {
            queues[q_name].my_io->reset_local_rd_ptr();
        }
    }

    workload->clear_temporal_graph_execution_status(graph_info.name);
}
void tt_golden::pre_instrn_instruction_callback(netlist_program &program) {
    auto workload = tt::golden::io::get_workload(m_netlist_path);
    tt_instruction_info instrn = program.get_current_instruction();
    if (instrn.opcode == INSTRUCTION_OPCODE::AllocateQueue) {
        for (const auto &var : instrn.vars) {
            string variable_string = std::get<0>(var);
            workload->allocate_queue(variable_string);
        }
    } else if (instrn.opcode == INSTRUCTION_OPCODE::DeallocateQueue) {
        for (const auto &var : instrn.vars) {
            string variable_string = std::get<0>(var);
            workload->deallocate_queue(variable_string);
        }
    }
}
void tt_golden::post_instrn_instruction_callback(netlist_program &program) {
    tt_instruction_info instrn = program.get_current_instruction();
    if (instrn.opcode == INSTRUCTION_OPCODE::VarInst) {
        string variable_string = std::get<0>(instrn.vars[0]);
        propagate_variable(program, variable_string);
    }
}

void tt_golden::load_parameter_and_constant_queues() {
    PROFILE_SCOPE(milliseconds);
    log_assert(config.tti != nullptr, "TTI must be initialized to load parameter and constant queues");
    for (const auto &io : config.tti->get_graph_constant_and_parameter_names()) {
        std::vector<float> data;
        auto q_desc = get_queue_descriptor(io);
        tt_PytorchTensorDesc pytorch_tensor_desc;
        tt_TilizedTensorDesc tilized_tensor_desc;
        bool tilized = tensor_meta_to_tensor_desc(config.tti->get_tensor_meta(io), data, pytorch_tensor_desc, tilized_tensor_desc, q_desc.bufq_target_format);
        if(tilized) {
            tt::golden::io::push_input(q_desc, tilized_tensor_desc, -1, 0);
        }
        else {
            tt::golden::io::push_input(q_desc, pytorch_tensor_desc, true, -1, 0);
        }
    }
}

// FIXME -- remove once all directed tests use sw untilization/tilization path.
//! Debug Interface Functions for queues -- Used by backend tests -- Will be REMOVED once the other paths are
//! supported
void tt_golden::push_input(const tt_queue_info &q_info, std::shared_ptr<tt_tensor> input, const int ptr) {
    return tt::golden::io::push_input(m_netlist_path, q_info, input, ptr);
}
std::shared_ptr<tt_tensor> tt_golden::pop_output(const tt_queue_info &q_info) {
    return tt::golden::io::pop_output(m_netlist_path, q_info);
}
std::shared_ptr<tt_tensor> tt_golden::get_output(const tt_queue_info &q_info, const int ptr) {
    return tt::golden::io::get_output(m_netlist_path, q_info, ptr);
}
}  // namespace tt::golden
