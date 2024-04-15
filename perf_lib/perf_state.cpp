// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "perf_state.hpp"
#include "utils.hpp"
#include "postprocess.hpp"
#include "runtime/runtime_utils.hpp"
#include "create_reports.hpp"

namespace postprocess {

extern std::recursive_mutex perf_state_mutex;

void PerfState::update_test_output_dir(const string &output_dir) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    test_output_dir = output_dir;
}

void PerfState::update_num_instructions_processed(const uint new_instructions_processed) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    num_instructions_processed += new_instructions_processed;
}

void PerfState::update_perf_check(bool check) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    perf_check_passed = perf_check_passed && check;
}

void PerfState::update_device_to_first_core(uint device_id, const tt_xy_pair &core_coord) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    if (device_to_first_core.find(device_id) == device_to_first_core.end()) {
        device_to_first_core.insert({device_id, core_coord});
    } else {
        device_to_first_core.at(device_id) = core_coord;
    }
}

void PerfState::update_device_to_epoch_id(uint device_id, uint epoch_id) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    if (device_to_epoch_id.find(device_id) == device_to_epoch_id.end()) {
        device_to_epoch_id.insert({device_id, epoch_id});
    } else {
        device_to_epoch_id.at(device_id) = epoch_id;
    }
    
}

void PerfState::update_graph_wrappers(const string &graph_name, const tt_digraph &graph) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    if (graph_wrappers.find(graph_name) == graph_wrappers.end()) {
        string graph_output_dir = postprocess::get_graph_descriptor_out_directory(test_output_dir, perf_desc.override_perf_output_dir, false);
        log_assert(fs::exists(graph_output_dir), "graph_output_dir {} is not initialized.", graph_output_dir);
        graph_output_dir += "/" + graph_name + "/";
        log_assert(!fs::exists(graph_output_dir), "graph_output_dir {} already exists", graph_output_dir);
        fs::create_directories(graph_output_dir);

        const uint target_device = graph.my_graph_info.target_device;
        log_assert(chip_id_to_sdesc.find(target_device) != chip_id_to_sdesc.end(), "soc descriptor does not exist for chip id {}", target_device);
        std::shared_ptr<tt_perf_digraph_wrapper> graph_wrapper = std::make_shared<tt_perf_digraph_wrapper>();
        graph_wrapper->graph = graph;
        unordered_map<string, core_descriptor> core_to_descriptor = postprocess::generate_graph_descriptor_map(
                                                                            graph_output_dir,
                                                                            graph.my_graph_info,
                                                                            queues,
                                                                            chip_id_to_sdesc.at(target_device),
                                                                            op_to_outputs);
        graph_wrapper->core_to_descriptor = core_to_descriptor;
        graph_wrappers.insert({graph_name, graph_wrapper});

        /* generate metadata reports for this graph */
        string metadata_output_dir = postprocess::get_metadata_out_directory(test_output_dir, perf_desc.override_perf_output_dir, false);
        postprocess::generate_metadata_reports(metadata_output_dir, graph_name, chip_id_to_sdesc.at(target_device), graph);
    }
}

void PerfState::update_queues(const string &queue_name, const tt_queue_wrap &queue) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    queues.insert({queue_name, queue});
}

void PerfState::update_program_name_to_instruction(const netlist_program &program) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    program_name_to_instructions.insert(
        {program.get_name(), program.get_program_trace()});
}
void PerfState::update_chip_id_to_sdesc(const uint &chip_id, const buda_SocDescriptor &soc_descriptor) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    if (chip_id_to_sdesc.find(chip_id) == chip_id_to_sdesc.end()) {
        chip_id_to_sdesc.insert({chip_id, soc_descriptor});
    }
}
void PerfState::update_chip_id_to_aiclk(const uint &chip_id, const uint &aiclk) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    chip_id_to_aiclk.insert({chip_id, aiclk});
}

void PerfState::update_op_to_outputs(const string &op_name, const unordered_set<string> &outputs) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    if (op_to_outputs.find(op_name) == op_to_outputs.end()) {
        op_to_outputs.insert({op_name, outputs});
    }
}

void PerfState::update_device_alignment_info_start(const int device_id, const uint64_t device_start, const uint64_t host_start) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    device_alignment_info.device_id_to_start_cycle.insert({device_id, device_start});
    device_alignment_info.device_id_to_host_start_cycle.insert({device_id, host_start});
}

void PerfState::update_device_alignment_info_end(const int &device_id, const uint64_t device_end, const uint64_t host_end) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    device_alignment_info.device_id_to_end_cycle.insert({device_id, device_end});
    device_alignment_info.device_id_to_host_end_cycle.insert({device_id, host_end});
}

void PerfState::set_perf_desc(const perf::PerfDesc &new_perf_desc) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    perf_desc = new_perf_desc;
}

void PerfState::update_total_input_count(int num_inputs) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    total_input_count = num_inputs;
}
void PerfState::set_postprocessor_executed() {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    postprocessor_executed = true;
}

bool is_perf_model_valid_for_op(const tt_op_info &op_info) {
    bool is_valid;
    if (op_info.type == "matmul" && op_info.attributes.identity) {
        return false;
    } else {
        return true;
    }
}

void PerfState::update_op_to_model_desc(const tt_digraph &graph) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    for (const auto &op_it: graph.my_graph_info.op_map) {
        string op_name = op_it.first;
        if (op_to_perf_model_desc.find(op_name) != op_to_perf_model_desc.end()) {
            return;
        }
        tt_op_model_desc perf_model = tt::get_perf_model_desc(op_it.second);
        const tt_op_info *op_info_ptr = &(graph.my_graph_info.op_map.at(op_name));
        PostprocessModelDesc postprocess_model_desc{
            .model_desc = perf_model,
            .op_info = op_info_ptr,
            .valid = is_perf_model_valid_for_op(*op_info_ptr)};
        op_to_perf_model_desc.insert({op_name, postprocess_model_desc});
    }
}

PostprocessModelDesc PerfState::get_op_perf_model_desc(const string &op_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(op_to_perf_model_desc.find(op_name) != op_to_perf_model_desc.end(), "perf model for op {} does not exist", op_name);
    return op_to_perf_model_desc.at(op_name);
}

const std::unordered_map<string, PostprocessModelDesc> &PerfState::get_all_perf_model_desc() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return op_to_perf_model_desc;
}

void PerfState::initialize_for_program(
        const netlist_program &program,
        const std::unordered_map<chip_id_t, buda_SocDescriptor> &sdesc_per_chip,
        const std::map<string, tt_digraph> &graphs,
        const unordered_map<string, unordered_set<string>> &op_to_outputs,
        const std::map<string, tt_queue_wrap> &input_queues) {
    
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_debug(tt::LogPerfInfra, "Initializing device perf state for program {}", program.get_name());
    log_assert(sdesc_per_chip.size() > 0, "soc descriptor must be loaded for at least one device");
    log_debug(tt::LogPerfInfra, "Soc descriptor is loaded for following device ids:");
    program_id++;
    local_epoch_id = 0;
    for (auto sdesc_it: sdesc_per_chip) {
        log_debug(tt::LogPerfInfra, "sdesc device id: {}", sdesc_it.first);
    }
    if (program_name_to_instructions.find(program.get_name()) != program_name_to_instructions.end()) {
        log_warning(tt::LogPerfInfra, "Skipping initialization for program name {} since it already exists", program.get_name());
        return;
    }

    for (const auto &device_sdesc_it: sdesc_per_chip) {
        update_chip_id_to_sdesc(device_sdesc_it.first, device_sdesc_it.second);
    }
    update_program_name_to_instruction(program);

    // Currently we have one set of queues for the entire workload. So we generate the queue descriptor file only once.
    // FIXME: We are assuming here that we will have a single workload for the entire runtime.
    // Therefore, the eqger mode in backend will not record the correct descriptor file for the queue.
    if (queues.empty()) {
        string queue_output_dir = postprocess::get_queue_descriptor_out_directory(test_output_dir, perf_desc.override_perf_output_dir, false);
        log_assert(fs::exists(queue_output_dir), "queue_output_dir {} is not initialized.", queue_output_dir);
        postprocess::generate_queue_descriptor(queue_output_dir, input_queues);
    }

    for (const auto &queue_it: input_queues) {
        update_queues(queue_it.first, queue_it.second);
    }
    for (const auto &op_to_output: op_to_outputs) {
        update_op_to_outputs(op_to_output.first, op_to_output.second);
    }
    for (const auto &graph_it: graphs) {
        update_graph_wrappers(graph_it.first, graph_it.second);
        update_op_to_model_desc(graph_it.second);
    }
    log_debug(tt::LogPerfInfra, "Finished initializing device perf state for program {}", program.get_name());
}

void PerfState::update_executed_instr(string program_name, int pc) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(program_name_to_instructions.find(program_name) != program_name_to_instructions.end(),
                "program name {} does not exist in device_perf_light", program_name);
    const tt_instruction_info instr = program_name_to_instructions.at(program_name).at(pc);
    const std::shared_ptr<tt_instruction_info> instr_ptr = std::make_shared<tt_instruction_info>(instr);
    if (instr.opcode != INSTRUCTION_OPCODE::Execute) {
        return;
    }

    const string &graph_name = instr.graph_name;
    if (!get_perf_desc().is_perf_enabled_for_graph(graph_name)) {
        global_epoch_id++;
        local_epoch_id++;
        return;
    }

    log_assert(program_name_to_instructions.find(program_name) != program_name_to_instructions.end(), "performance state is not initialized for program with name {}", program_name);
    log_assert(program_name_to_instructions.at(program_name).size() > uint(pc), "program counter must be smaller than the total number of instructions");
    log_assert(instr.opcode == INSTRUCTION_OPCODE::Execute, "Only execute instructions must be pushed to perf state. opcode for the current instruction: {}", instr.opcode);
    log_assert(graph_wrappers.find(instr.graph_name) != graph_wrappers.end(), "Graph with name ({}) must have been inserted in perf state during initialization", instr.graph_name);

    instruction_info_wrapper instr_wrapper {
        .instr = instr_ptr,
        .program_id = uint(program_id),
        .program_name = program_name,
        .local_epoch_id = local_epoch_id,
        .global_epoch_id = global_epoch_id};
    log_debug(tt::LogPerfInfra,
                "Pushing instruction for program {} graph {} local_epoch_id {} global_epoch_id {} to device perf state.",
                program_name, instr.graph_name, local_epoch_id, global_epoch_id);
    executed_instr.push_back(instr_wrapper);
    last_program_name = program_name;
    global_epoch_id++;
    local_epoch_id++;
    if (program_id_to_num_instructions_executed.find(program_id) == program_id_to_num_instructions_executed.end()) {
        program_id_to_num_instructions_executed.insert({program_id, 0});
    }
    if (program_id_to_num_instructions_executed.at(program_id) < local_epoch_id) {
        program_id_to_num_instructions_executed.at(program_id) = local_epoch_id;
    }
}

void PerfState::update_all_epochs_info(const vector<EpochPerfInfo> &epoch_perf_info) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    all_epochs_perf_info = epoch_perf_info;
}

void PerfState::update_last_epoch_kernel_runtime(const tt_xy_pair &core_coord, const vector<uint64_t> kernel_runtime) {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    last_epoch_kernel_runtime.insert({core_coord, kernel_runtime});
}

bool PerfState::is_physical_core_active_in_graph(const string &graph_name, const tt_xy_pair &core) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    string core_label = to_string(core.x) + "-" + to_string(core.y);
    unordered_map<string, core_descriptor> desc = get_core_to_desc_map(graph_name);
    return desc.find(core_label) != desc.end();
}
bool PerfState::is_postprocessor_executed() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return postprocessor_executed;
}

bool PerfState::is_queue(const string &queue_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return queues.find(queue_name) != queues.end();
}

const vector<EpochPerfInfo> &PerfState::get_all_epochs_perf_info() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return all_epochs_perf_info;
}

const tt_perf_device_alignment &PerfState::get_device_alignment_info() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return device_alignment_info;
}

unordered_map<tt_xy_pair, vector<uint64_t>> PerfState::get_last_epoch_kernel_runtime() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return last_epoch_kernel_runtime;
}

uint PerfState::get_num_instructions_executed(const uint &program_id) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(program_id_to_num_instructions_executed.find(program_id) != program_id_to_num_instructions_executed.end(), "Program id {} was not recorded by perf state", program_id);
    return program_id_to_num_instructions_executed.at(program_id);
}

const std::vector<instruction_info_wrapper> &PerfState::get_executed_instr() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return executed_instr;
}

std::vector<instruction_info_wrapper> PerfState::get_unprocessed_executed_instr() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    vector<instruction_info_wrapper> result(executed_instr.begin() + num_instructions_processed, executed_instr.end());
    return result;
}

unordered_set<string> PerfState::get_outputs_for_op(const string &op_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(op_to_outputs.find(op_name) != op_to_outputs.end(), "device perf state is not initialized for op_name {}", op_name);
    return op_to_outputs.at(op_name);
}

const unordered_map<string, core_descriptor> &PerfState::get_core_to_desc_map(const string &graph_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(graph_wrappers.find(graph_name) != graph_wrappers.end(), "Core descriptor does not exist for graph {}", graph_name);
    return graph_wrappers.at(graph_name)->core_to_descriptor;
}

const core_descriptor &PerfState::get_core_desc(const string &graph_name, const tt_xy_pair &core) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    string core_label = to_string(core.x) + "-" + to_string(core.y);
    const unordered_map<string, core_descriptor> &core_to_desc = get_core_to_desc_map(graph_name);
    log_assert(core_to_desc.find(core_label) != core_to_desc.end(), "core descriptor for physical core {} does not exist under graph {} in device perf state", core_label, graph_name);
    return core_to_desc.at(core_label);
}

const buda_SocDescriptor &PerfState::get_sdesc_for_device(const int device_id) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(chip_id_to_sdesc.find(device_id) != chip_id_to_sdesc.end(), "soc descriptor for device id {} does not exist", device_id);
    return chip_id_to_sdesc.at(device_id);
}

std::shared_ptr<tt_perf_digraph_wrapper> PerfState::get_graph_wrapper(const string &graph_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(graph_wrappers.find(graph_name) != graph_wrappers.end(), "graph_name {} does not exist in device perf state", graph_name);
    return graph_wrappers.at(graph_name);
}

const tt_digraph &PerfState::get_graph(const string &graph_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    std::shared_ptr<tt_perf_digraph_wrapper> graph_wrapper = get_graph_wrapper(graph_name);
    return graph_wrapper->graph;
}

uint PerfState::get_aiclk(const uint &target_device) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(chip_id_to_aiclk.find(target_device) != chip_id_to_aiclk.end(), "aiclk is not populated for chip_id {} inside device perf state", target_device);
    return chip_id_to_aiclk.at(target_device);
}

int PerfState::get_total_input_count() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return total_input_count;
}

const perf::PerfDesc &PerfState::get_perf_desc() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return perf_desc;
}

tt_queue_wrap PerfState::get_queue(const string &queue_name) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(queues.find(queue_name) != queues.end(), "Perf State Queue list is not initialized for queue name {}", queue_name);
    return queues.at(queue_name);
}

std::pair<uint32_t, const instruction_info_wrapper> PerfState::get_global_epoch_idx_and_instr_for_core(perf::PerfDumpHeader header) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    uint instr_idx = 0;
    uint instr_active_idx = 0;
    for (const auto &instr_wrap : get_executed_instr()) {

        string graph_name = instr_wrap.instr->graph_name;
        const tt_digraph &graph = get_graph(graph_name);
        if (graph.my_graph_info.target_device != header.chip_id) {
            instr_idx++;
            continue;
        }
        if (is_physical_core_active_in_graph(graph_name, tt_xy_pair(header.x, header.y))) {
            if (instr_active_idx == header.epoch_id) {
                log_assert(get_perf_desc().is_perf_enabled_for_graph(graph_name),
                    "Perf trace for epoch_idx {} for core {} running graph {} was disabled but a perf dump is received from device!",
                    uint(header.epoch_id), tt_cxy_pair(header.chip_id, header.x, header.y).str(), graph_name);
                return {instr_idx, instr_wrap};
            }
            instr_active_idx++;
        }
        instr_idx++;
    }
    log_assert(false, "epoch idx {} for core x {} y {} chip {} does not exist in the list of executed instructions in device perf state",
                uint(header.epoch_id), uint(header.x), uint(header.y), uint(header.chip_id));
    instruction_info_wrapper instr_wrap;
    return {instr_idx, instr_wrap};
}

std::unordered_map<string, tt_digraph> PerfState::get_all_graphs() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    std::unordered_map<string, tt_digraph> all_graphs;
    for (const auto &graph_it: graph_wrappers) {
        all_graphs.insert({graph_it.first, graph_it.second->graph});
    }
    return all_graphs;
}

const std::unordered_map<string, tt_queue_wrap> &PerfState::get_all_queues() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return queues;
}

const string &PerfState::get_test_output_dir() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return test_output_dir;
}

uint PerfState::get_num_instructions_processed() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return num_instructions_processed;
}

bool PerfState::get_perf_check() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    return perf_check_passed;
}

tt_xy_pair PerfState::get_first_core_for_device_id(uint device_id) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(device_to_first_core.find(device_id) != device_to_first_core.end(), "device_id {} does not exist in device_to_first_core map", device_id);
    return device_to_first_core.at(device_id);
}

uint PerfState::get_epoch_id_for_device_id(uint device_id) const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(device_to_epoch_id.find(device_id) != device_to_epoch_id.end(), "device_id {} does not exist in device_to_epoch_id map", device_id);
    return device_to_epoch_id.at(device_id);
}

tt::TargetDevice PerfState::get_target_device_type() const {
    const std::lock_guard<std::recursive_mutex> lock(perf_state_mutex);
    log_assert(target_device_type != tt::TargetDevice::Invalid, "target device type is not initialized in device perf state");
    return target_device_type;
}

} // namespace postprocess

