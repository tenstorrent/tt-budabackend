// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "scratch_api.h"
#include "netlist/tt_digraph.hpp"

enum class BackendProfilerLevel {
    Info = 0,
    Debug = 1,
};
#ifdef BACKEND_PROFILER_DEBUG
    constexpr BackendProfilerLevel backend_profiler_level = BackendProfilerLevel::Debug;
#else
    constexpr BackendProfilerLevel backend_profiler_level = BackendProfilerLevel::Info;
#endif

namespace perf {

using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::unordered_map;
using std::unordered_set;
using std::ofstream;
using std::ifstream;
using std::pair;
using std::to_string;

using tt::EpochType;

/////////////////////////////////////////////////////////////////////////
//////////////// Const variables
/////////////////////////////////////////////////////////////////////////
// Each thread perf trace dump must start with the following value
constexpr uint32_t valid_thread_dump_start_id = 0xbeeff00d;
// Each thread perf trace dump must end with the following value
constexpr uint32_t thread_dump_end_id = valid_thread_dump_start_id;
// Host must reset the scratch buffer first word to the following value
constexpr uint32_t perf_scratch_empty_id = 0x0;
constexpr uint32_t perf_scratch_end_empty_id = 0xc0ba14f3;

// Must be the same values as in
// src/firmware/riscv/wormhole/stream_io_map.h
// src/firmware/riscv/grayskull/stream_io_map.h
constexpr uint8_t PERF_MAX_NUM_INPUTS = 8;
constexpr uint8_t PERF_MAX_NUM_OUTPUTS = 1;


const int NumBytesPerEvent = 4;
const uint PerfValFirst = 0xbeeff00d;
const uint PerfValLast = 0xbeeff00d;
const uint PerfValPadding = 0xdeadbead;
const uint PerfOutOfMem = 0xfeedfeed;
const int NumValuesPerEventUnpackPack = 3;
const int NumValuesPerEventNcrisc = 2;
const int NumValuesPerEventMath = 4;

const string postprocess_json_name = "perf_postprocess.json";
const string cores_to_ops_json_name = "cores_to_ops.json";
const string queue_descriptor_json_name = "queue_descriptor.json";
const string runtime_table_json_name = "runtime_table.json";
const string runtime_table_csv_name = "runtime_table.csv";
const string runtime_table_log_name = "runtime_table.txt";
const string op_table_json_name = "op_perf_table.json";
const string perf_info_all_epochs_csv_name = "perf_info_all_epochs.csv";
const string perf_info_all_epochs_yaml_name = "perf_info_all_epochs.yaml";
const string postprocess_original_json_name = "perf_postprocess_original.json";
const string host_summary_json_name = "host_summary_report.json";
const string perf_model_log = "perf_model.log";

/////////////////////////////////////////////////////////////////////////
//////////////// Enums and maps
/////////////////////////////////////////////////////////////////////////
enum class PerfDumpMode {
    Disable = 0,
    IntermediateDump = 1,
    SingleDumpPerEpoch = 2,
    Concurrent = 3,
};

enum class PerfTriscDecoupleMode {
    None = 0,
    UnpMath = 1,
    MathPack = 2,
};
string PerfTriscDecoupleModetoString(const PerfTriscDecoupleMode& decouple_mode);
PerfTriscDecoupleMode StringtoPerfTriscDecoupleMode(const string& decouple_mode);

struct PerfOverlayDecoupleMode {
    
    unordered_set<uint> input_operand_decouple;
    bool all_inputs = false;
    bool output = false;
    
    bool operator==(const struct PerfOverlayDecoupleMode& other) const {
        return (this->all_inputs == other.all_inputs)
            && (this->output == other.output)
            && (this->input_operand_decouple == other.input_operand_decouple);
    }

};

inline ostream& operator<<(ostream& ss, const PerfOverlayDecoupleMode& t) {

    ss << "all_inputs  =  " << t.all_inputs << " ";
    ss << "Output = " << t.output << " ";
    ss << "Input operand idx = ";
    for (auto input_idx: t.input_operand_decouple) {
        ss << input_idx << " ";
    }
    return ss;
}

enum class PerfTraceMode {
    None    = 0,
    Light   = 1,
    Verbose = 2,
};

enum class CoordType {
    Physical = 0,
    Logical  = 1,
};

const unordered_set<uint> single_value_events = {
    uint(EventType::UNPACK_FIRST_INSTRUCTION),
    uint(EventType::NUM_TILES_UNPACK),
    uint(EventType::NUM_TILES_PACK),
    uint(EventType::OUTPUT_NUM_TILES),
};

const unordered_set<uint> brisc_single_value_events = {
    uint(BriscEventType::INPUT_NUM_TILES),
    uint(BriscEventType::OUTPUT_NUM_TILES),
};

enum class HostEventType {
    UNDEFINED = 0,
    HW_TILIZE,
    SW_TILIZE,
    DEVICE_RUNTIME,
    DEVICE_START_CYCLE,
    DEVICE_END_CYCLE,
    PIPEGEN_RUNTIME,
    WAIT_FOR_EPOCH_COMPLETE,
    WAIT_FOR_EPOCH_UNALIASED,
    WAIT_FOR_EPOCH_QUEUES_READY,
    WAIT_FOR_QUEUE_READY,
    WAIT_FOR_QUEUE_READY_WC,
    RUN_EXECUTE_INSTRUCTION,
    PUSH_QUEUE_SIDE_EFFECTS,
    RUN_PROGRAM,
    GET_UNTILIZED_OUTPUT,
    GET_TILIZED_OUTPUT,
    POP_OUTPUT,
    QUEUE_UPDATE_COMMAND,
    QUEUE_ALLOCATE_COMMAND,
    SEND_EPOCH_BINARIES,
    SEND_EPOCH_COMMANDS,
    PERF_POSTPROCESSOR,
    DEVICE_START_CYCLE_ALIGNED,
    DEVICE_END_CYCLE_ALIGNED,
    DEVICE_EPOCH_FIRST_UNPACK_LAST_PACK,
    LAYOUT_BINARIES,
    QUEUE_UPDATE_VARINST,
    QUEUE_CHECK_VARINST,
    CUSTOM,
};

const unordered_set<uint> host_single_value_events = {
    uint(HostEventType::DEVICE_START_CYCLE),
    uint(HostEventType::DEVICE_END_CYCLE),
    uint(HostEventType::DEVICE_START_CYCLE_ALIGNED),
    uint(HostEventType::DEVICE_END_CYCLE_ALIGNED),
};

const unordered_map<int, string> host_event_labels = {
    {uint(HostEventType::HW_TILIZE),                 "push-input-hw-tilize"},
    {uint(HostEventType::SW_TILIZE),                 "push-input-sw-tilize"},
    {uint(HostEventType::DEVICE_RUNTIME),            "device-runtime"},
    {uint(HostEventType::DEVICE_START_CYCLE),        "device-start-cycle"},
    {uint(HostEventType::DEVICE_END_CYCLE),          "device-end-cycle"},
    {uint(HostEventType::PIPEGEN_RUNTIME),           "pipegen-runtime"},
    {uint(HostEventType::WAIT_FOR_EPOCH_COMPLETE),   "wait-for-epoch-complete"},
    {uint(HostEventType::WAIT_FOR_EPOCH_UNALIASED),  "wait-for-aliased-epoch-complete"},
    {uint(HostEventType::WAIT_FOR_EPOCH_QUEUES_READY),"wait-for-epoch-command-queue-not-full"},
    {uint(HostEventType::WAIT_FOR_QUEUE_READY),      "wait-for-queue-not-full"},
    {uint(HostEventType::WAIT_FOR_QUEUE_READY_WC),   "wait-for-queue-wc-num-slots"},
    {uint(HostEventType::RUN_EXECUTE_INSTRUCTION),   "run-execute-instruction"},
    {uint(HostEventType::PUSH_QUEUE_SIDE_EFFECTS),   "update-pending-queue-settings"},
    {uint(HostEventType::RUN_PROGRAM),               "run-program"},
    {uint(HostEventType::GET_UNTILIZED_OUTPUT),      "get-untilized-output-tensor-from-device"},
    {uint(HostEventType::GET_TILIZED_OUTPUT),        "get-tilized-output-tensor-from-device"},
    {uint(HostEventType::POP_OUTPUT),                "pop-output-tensor-from-device"},
    {uint(HostEventType::QUEUE_UPDATE_COMMAND),      "send-queue-update-command"},
    {uint(HostEventType::QUEUE_ALLOCATE_COMMAND),    "send-queue-allocate-command"},
    {uint(HostEventType::SEND_EPOCH_BINARIES),       "send-epoch-binary"},
    {uint(HostEventType::SEND_EPOCH_COMMANDS),       "send-epoch-command"},
    {uint(HostEventType::PERF_POSTPROCESSOR),        "perf-postprocessor"},
    {uint(HostEventType::DEVICE_START_CYCLE_ALIGNED), "device-start-cycle-aligned"},
    {uint(HostEventType::DEVICE_END_CYCLE_ALIGNED),   "device-end-cycle-aligned"},
    {uint(HostEventType::DEVICE_EPOCH_FIRST_UNPACK_LAST_PACK),   "device-epoch-first-unpack-last-pack"},
    {uint(HostEventType::LAYOUT_BINARIES),           "layout-binaries"},
    {uint(HostEventType::QUEUE_UPDATE_VARINST),      "send-queue-update-varinst-command"},
    {uint(HostEventType::QUEUE_CHECK_VARINST),       "check-queue-update-varinst-command"},
    {uint(HostEventType::CUSTOM),                    ""},
};

const vector<string> thread_names = {"T0", "T1", "T2", "NCRISC", "BRISC"};
const map<PerfTraceMode, vector<string>> perf_trace_mode_to_perf_config  {
    {PerfTraceMode::None,       {}},
    {PerfTraceMode::Light,      {"--dump-perf-events",  "--perf-target-inputs", "0,1,2,-1,-2"}},
    {PerfTraceMode::Verbose,    {"--dump-perf-events-intermediate", "--perf-level", "1", "--perf-target-inputs", "0,1,2,3,-1,-2,-3"}},
};
const unordered_map<int, string> event_labels = {
    {uint(EventType::WAIT_FOR_INCOMING_TILES), "wait-for-incoming-tiles"},
    {uint(EventType::WAIT_FOR_FREE_TILES), "wait-for-free-tiles"},
    {uint(EventType::PACK_EACH_INPUT), "packer-each-input"},
    {uint(EventType::MATH_PERF_COUNTERS), "math-perf-counter"},
    {uint(EventType::UNPACK_FIRST_INSTRUCTION), "unpack-first-instruction"},
    {uint(EventType::STALL_TRISC_FOR_DRAM_PERF_DUMP), "trisc-stall-on-dram-perf-dump"},
    {uint(EventType::NUM_TILES_UNPACK), "num-tiles-input"},
    {uint(EventType::NUM_TILES_PACK), "num-tiles-output"},

};

const map<int, string> brisc_event_labels = {
    {uint(BriscEventType::INPUT_NUM_TILES), "input-num-tiles"},
    {uint(BriscEventType::OUTPUT_NUM_TILES), "output-num-tiles"},
    {uint(BriscEventType::INPUT_TILE_POP), "input-tile-pop"},
    {uint(BriscEventType::OUTPUT_TILE_PUSH), "output-tile-push"},
    {uint(BriscEventType::STALL_BRISC_FOR_DRAM_PERF_DUMP), "brisc-stall-on-dram-perf-dump"},
    {uint(BriscEventType::EPOCH), "epoch"},
};

const unordered_map<int, string> ncrisc_event_labels = {
    {uint(NcriscEventType::EPOCH), "epoch"},
    {uint(NcriscEventType::STREAM_HANDLER_INIT), "epoch-prologue"},
    {uint(NcriscEventType::STREAM_HANDLER_LOOP), "epoch-loop"},
    {uint(NcriscEventType::EPOCH_EPILOGUE), "epoch-epilogue"},
    {uint(NcriscEventType::EPOCH_Q_SLOT_COMPLETE), "epoch-q-slot-complete"},
    {uint(NcriscEventType::DRAM_READ_ISSUED), "dram-read"},
    {uint(NcriscEventType::DRAM_READ_TILE_FLUSHED), "dram-read-tile-flushed"},
    {uint(NcriscEventType::DRAM_WRITE_SENT), "dram-write-sent"},
    {uint(NcriscEventType::DRAM_WRITE_TILES_CLEARED), "dram-write-tile-cleared"},
    {uint(NcriscEventType::DRAM_IO_Q_STATUS), "dram-io-q-status"},
    {uint(NcriscEventType::STREAM_RESTART), "stream-restart"},
    {uint(NcriscEventType::STREAM_INFO), "info"},
    {uint(NcriscEventType::STREAM_BUF_STATUS), "buffer-status"},
    {uint(NcriscEventType::EPOCH_Q_EMPTY), "epoch-q-empty"},
    {uint(NcriscEventType::STREAM_MISC_INFO), "misc-info"},
};

/////////////////////////////////////////////////////////////////////////
//////////////// Custom data structures and classes
/////////////////////////////////////////////////////////////////////////
struct host_global_events {
    uint64_t first_program_start = ULLONG_MAX;
    uint64_t last_output_pop_end = ULLONG_MAX;
};

struct event {
    uint64_t id;
    string description;
    uint64_t first_val = ULLONG_MAX;
    uint64_t second_val = ULLONG_MAX;
    vector<uint32_t> extra_val;
};

struct core_descriptor {
    string op_name;
    string op_type;
    tt_xy_pair physical_core;
    tt_xy_pair logical_core;

    core_descriptor() {}
    core_descriptor(const string &op_name, const string &op_type, const tt_xy_pair &physical_core, const tt_xy_pair &logical_core)
        : op_name(op_name), op_type(op_type), physical_core(physical_core), logical_core(logical_core)
    {
    }

    inline string get_core_str(CoordType coord_type, bool x_y = true) const {
        tt_xy_pair core;
        if (coord_type == CoordType::Physical) {
            core = physical_core;
        }
        else if (coord_type == CoordType::Logical) {
            core = logical_core;
        }
        else {
            log_fatal("Invalid core type.");
        }

        string core_str = x_y ? to_string(core.x) + "-" + to_string(core.y) : to_string(core.y) + "-" + to_string(core.x);
        return core_str;
    }

};

struct thread_events {
    int thread_id = -1;
    perf::PerfDumpHeader header;
    bool out_of_memory = false;
    int num_inputs_recorded = -1;
    map<uint64_t, vector<event>> events;

    void set_out_of_memory() {
        out_of_memory = true;
    }
};

struct outer_loop_events {
    uint64_t unpack_first_instruction       = ULLONG_MAX; // Timestamp of when first block of data is available on all operands
    uint64_t pack_first_start               = ULLONG_MAX; // Pack start for each input
    uint64_t pack_last_end                  = ULLONG_MAX; // pack end for each input
    uint64_t total_wait_for_tile_after_first_unpack         = 0; // Sum of all wait-for-tiles on unpacker side, after the first block of data is available
    uint64_t total_wait_for_free_tiles_after_first_unpack   = 0; // Sum of all wait-for-free-tiles on packer side, after the first block of data is available
    uint64_t total_trisc0_stalled_on_ncrisc = 0;
    uint64_t total_trisc2_stalled_on_ncrisc = 0;
};

struct tt_perf_device_alignment {
    map<int, uint64_t> device_id_to_start_cycle;
    map<int, uint64_t> device_id_to_end_cycle;
    map<int, uint64_t> device_id_to_host_start_cycle;
    map<int, uint64_t> device_id_to_host_end_cycle;
};

struct PostprocessModelDesc {
    tt_op_model_desc model_desc;
    const tt_op_info *op_info;
    bool valid;
};

struct core_events {
    int core_x = -1;
    int core_y = -1;
    core_descriptor descriptor;
    map<string, thread_events> threads;
    map<uint, outer_loop_events> all_outer_loop_events;
    uint64_t math_activity  = ULLONG_MAX;
    unordered_map<uint, uint> trisc_operand_to_num_tiles;
    bool out_of_memory = false;
    unordered_map<uint, uint> brisc_operand_to_num_tiles;
    unordered_map<uint, uint64_t> brisc_operand_to_pop_tiles_num_cycles;
    uint32_t packer_num_tiles = 0;
    uint64_t packer_push_runtime = 0;
    uint32_t model_execution_cycles = 0;

    void check_and_set_out_of_memory() {
        // Return if all threads have not been processed yet
        if (threads.size() != thread_names.size()) {
            return;
        }
        for (const auto& thread_it: threads) {
            out_of_memory = out_of_memory || thread_it.second.out_of_memory;
        }
    }
};

struct instruction_info_wrapper {
    const std::shared_ptr<tt_instruction_info> instr;
    uint program_id;
    string program_name;
    uint local_epoch_id;
    uint global_epoch_id;
};

struct tt_perf_digraph_wrapper {
    tt_digraph graph;
    // Descriptor for every core active withing each graph
    unordered_map<string, core_descriptor> core_to_descriptor;
};

struct epoch_events {
    unordered_map<tt_xy_pair, core_events> all_core_events;
    const instruction_info_wrapper instr_wrap;
    const std::shared_ptr<tt_perf_digraph_wrapper> graph_wrapper;
    const uint aiclk;
    const uint num_epochs_current_program;
    const perf::tt_perf_device_alignment device_alignment_info;
    const std::unordered_map<string, PostprocessModelDesc> op_to_perf_model_desc;

    inline const uint get_num_active_cores() const {
        return graph_wrapper->core_to_descriptor.size();
    }
    inline const uint get_device_id() const {
        return graph_wrapper->graph.my_graph_info.target_device;
    }
    inline const string &get_graph_name() const {
        return instr_wrap.instr->graph_name;
    }
    inline const uint &get_global_epoch_id() const {
        return instr_wrap.global_epoch_id;
    }
    bool is_fully_populated() {
        uint num_active_cores = get_num_active_cores();
        log_assert(num_active_cores >= all_core_events.size(), "Number or core_events recorded for each epoch cannot exceed the number of active cores for that epoch");
        return all_core_events.size() == num_active_cores;
    }
};


struct InstructionInfo {
    int program_id;
    int instr_id_local;
    int instr_id_global;
    int num_instr_current_program;
    string program_name;
    string graph_name;
    int device_id;
    int aiclk;
    int input_count;
    tt_digraph netlist_graph;

    string intermed_file_path = "";
    string output_dir_path = "";

    pair<int, int> first_and_last_inputs_recorded = {-1,-1};
    float num_cycles_per_input = 0;
    // Average number of inputs the hardware processes per second
    // (num_inputs - 1) / (Last_pack_last_input_seconds - Last_pack_first_input_seconds)
    // Will only get calculated for graphs with num_inputs greater than 1
    float num_inputs_per_second = 0;
    
    // Last_pack - first_unpack of the last input that both of these values are recorded
    uint64_t last_recorded_input_execution_cycle = 0;
    
    // Largest number of cycles each core has been stalled waiting for epoch binaries
    uint64_t largest_wait_for_epoch_binary_cycles = 0;

    // Throughput information in steady state
    // Only populated when --measure-steady-state is enabled
    // In this mode, following throughput will be calculated between input_count/4 and 3*input_count/4
    pair<int, int> steady_state_first_and_last_inputs = {-1, -1};
    float num_cycles_per_input_steady_state = 0;
    float num_inputs_per_second_steady_state = 0;

    uint64_t first_unpack_first_input = ULLONG_MAX;
    uint64_t last_pack_first_input = ULLONG_MAX;
    uint64_t last_pack_last_input = ULLONG_MAX;

    InstructionInfo(
        int program_id, int instr_id_local, int instr_id_global, int num_instr_current_program, string program_name, string graph_name, int device_id, tt_digraph netlist_graph, string perf_out_dir, int aiclk, int input_count
    ): program_id(program_id), instr_id_local(instr_id_local), instr_id_global(instr_id_global), num_instr_current_program(num_instr_current_program), program_name(program_name),
        graph_name(graph_name), device_id(device_id), netlist_graph(netlist_graph),
        aiclk(aiclk), input_count(input_count) {
        
        set_intermed_file_name(perf_out_dir);
    }
    string get_intermed_file_name();
    void set_intermed_file_name(const string& perf_out_dir);
    const string get_output_dir_name() const;
    string get_program_dir_name();
    void set_output_dir_path(const string& perf_out_dir);
    const string get_epoch_label() const;
};

template<typename T>
struct PerfCheckValue {
    // Only check against this value if en is true
    bool en = false;
    // Treat target_value as a minimum threshold if using_min_bound is true
    bool using_min_bound = false;
    //Check if reported value is within rtol of target_value if using_expected value is true.
    //Both flags cannot be set to true at once.
    bool using_expected_value = false;
    T target_value = 0;
    float rtol = 0.01;

    inline void set_comparison_type(const std::string& comparison_type){
        if(using_expected_value && comparison_type == "min_bound" || using_min_bound && comparison_type == "expected") {
                log_fatal("Cannot set min_bound and expected performance check to true.");
        }
        if(comparison_type == "min_bound")
            using_min_bound = true;
        if(comparison_type == "expected")
            using_expected_value = true;
    }
    T override_target_value = 0;
};

struct PerfComparisonConfig {
    string program_name;
    string graph_name;

    // Check these values for specific cores
    PerfCheckValue<float> math_utilization;
    PerfCheckValue<uint64_t> execution_cycles;

    // Check these values for specific graphs
    PerfCheckValue<float> num_cycles_per_input;
    PerfCheckValue<float> num_inputs_per_second;

    // Check these values across all inputs for each core
    PerfCheckValue<float> average_math_utlization;
    PerfCheckValue<uint64_t> total_runtime;

    PerfCheckValue<float> input0_bw;
    PerfCheckValue<float> output_bw;

    // Host event checks
    PerfCheckValue<float> backend_samples_per_second;

    vector<tt_xy_pair> target_cores;
    vector<string> target_ops;

    vector<int> target_inputs = {0};

    void print();
};

struct PerfDramDecouple {
    bool all_inputs = false;
    bool all_outputs = false;
    string graph_name = "";
    string queue_name = "";
    
    void merge_with_another_config(const struct PerfDramDecouple& other_config) {
        
        log_assert(graph_name == other_config.graph_name, "Graph name mismatch");
        log_assert(queue_name == other_config.queue_name, "Queue name mismatch");
        all_inputs = all_inputs || other_config.all_inputs;
        all_outputs = all_outputs || other_config.all_outputs;        
    }

    inline const void print() const {
        std::stringstream ss;
        ss << "Graph name = " << graph_name << " queue name = " << queue_name << " All Inputs: " << all_inputs << " All Outputs: " << all_outputs;
        log_info(tt::LogRuntime, "{}", ss.str());
    }
    
    bool operator==(const struct PerfDramDecouple& other) const {
        return (this->all_inputs == other.all_inputs)
            && (this->all_outputs == other.all_outputs)
            && (this->graph_name == other.graph_name)
            && (this->queue_name == other.queue_name);
    }
};

}

template<>
struct std::hash<perf::PerfDumpMode>
{
    std::size_t operator()(perf::PerfDumpMode const& obj) const noexcept
    {
        return static_cast<std::size_t>(obj);
    }
};

template<>
struct std::hash<perf::PerfTriscDecoupleMode>
{
    std::size_t operator()(perf::PerfTriscDecoupleMode const& obj) const noexcept
    {
        return static_cast<std::size_t>(obj);
    }
};

template<>
struct std::hash<perf::PerfOverlayDecoupleMode>
{
    std::size_t operator()(perf::PerfOverlayDecoupleMode const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        boost::hash_combine(hash_value, std::hash<bool>{}(obj.all_inputs));
        boost::hash_combine(hash_value, std::hash<bool>{}(obj.output));
        for (const auto &v: obj.input_operand_decouple) {
            boost::hash_combine(hash_value, std::hash<uint>{}(v));
        }
        return hash_value;
    }
};

template<>
struct std::hash<perf::PerfDramDecouple>
{
    std::size_t operator()(perf::PerfDramDecouple const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        boost::hash_combine(hash_value, std::hash<bool>{}(obj.all_inputs));
        boost::hash_combine(hash_value, std::hash<bool>{}(obj.all_outputs));
        boost::hash_combine(hash_value, std::hash<string>{}(obj.graph_name));
        boost::hash_combine(hash_value, std::hash<string>{}(obj.queue_name));
        return hash_value;
    }
};

template<>
struct std::hash<perf::PerfComparisonConfig>
{
    std::size_t hash_for_unique_compile(perf::PerfComparisonConfig const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        boost::hash_combine(hash_value, std::hash<string>{}(obj.program_name));
        boost::hash_combine(hash_value, std::hash<string>{}(obj.graph_name));
        // boost::hash_combine(hash_value, std::hash<>{}(obj.math_utilization));
        // boost::hash_combine(hash_value, std::hash<>{}(obj.execution_cycles));
        for (auto& e : obj.target_cores)
        {
            boost::hash_combine(hash_value, std::hash<tt_xy_pair>{}(e));
        }
        for (auto& e : obj.target_ops)
        {
            boost::hash_combine(hash_value, std::hash<string>{}(e));
        }
        for (auto& e : obj.target_inputs)
        {
            boost::hash_combine(hash_value, std::hash<int>{}(e));
        }

        return hash_value;
    }
};