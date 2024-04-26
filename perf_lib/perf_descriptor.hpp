// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <boost/functional/hash.hpp>
#include <thread>
#include "scratch_api.h"
#include "perf_base.hpp"
#include "netlist/netlist_workload_data.hpp"
#include "common/env_lib.hpp"

extern std::mutex backend_profiler_mutex;

// This table is being used to generate the summary report after every test
namespace table {

class PrettyTable {
    public:
        enum class Format {
            Pretty,
            CSV,
        };

        vector<vector<string>> table_;
        int horizontal_line_row = 1;
        int vertical_line_col = 1;
        int padding_between_cells = 2;

        void add_row(vector<string> incoming_new_row);
        void add_divider();
        bool is_divider_row(int row_index) const;
        string generate_table_string(Format format = Format::Pretty);
};

}
namespace perf {

using std::string;
using std::vector;
using std::map;
using std::unordered_set;
using std::to_string;
using tt::EpochPerfInfo;
typedef std::chrono::system_clock LoaderClock;
typedef std::chrono::time_point<LoaderClock> TimePoint;
typedef std::chrono::nanoseconds ns;
typedef std::chrono::duration<double> Duration;

////////////////////////////////////////////////////////
//////////// Utility functions
////////////////////////////////////////////////////////

// Get thread id of the current process
inline const uint get_thread_id() {
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

// Used in concurrent mode, utility to print the header
inline std::stringstream get_perf_dump_header_ss(perf::PerfDumpHeader header) {
    stringstream ss;
    ss << endl;
    ss << "x: " << (uint32_t)header.x << endl;
    ss << "y: " << (uint32_t)header.y << endl;
    ss << "chip_id: " << (uint32_t)header.chip_id << endl;
    ss << "thread_id: " << header.thread_id << endl;
    ss << "epoch_id: " << header.epoch_id << endl;
    return ss;
}

inline stringstream get_hex_representation_of_num(uint num) {
    stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << num;
    return ss;
}

inline stringstream get_thread_dump_partial_ss(uint32_t* queue_base_ptr, uint single_thread_dump_size) {

    stringstream ss;
    ss << endl;

    for (uint idx = 0; idx < single_thread_dump_size / sizeof(uint32_t); idx++) {
        ss << get_hex_representation_of_num(queue_base_ptr[idx]).rdbuf() << endl;
    }
    return ss;
}

inline stringstream get_thread_dump_partial_ss(vector<uint32_t> queue) {

    stringstream ss;
    ss << endl;

    for (auto data: queue) {
        ss << get_hex_representation_of_num(data).rdbuf() << endl;
    }
    return ss;
}

////////////////////////////////////////////////////////
//////////// Performance Descriptor
////////////////////////////////////////////////////////

vector<int> decode_target_inputs_str(const vector<string>& target_inputs_encoded, int num_inputs);

// Configures every aspect of the performance trace and testing infrastructure
struct PerfDesc {
    // Four different modes
    // tenstorrent/budabackend/-/wikis/Performance-Infrastructure-and-Debug#performance-trace-architecture
    PerfDumpMode device_perf_mode = PerfDumpMode::Disable;
    uint32_t perf_dump_level = 0;
    // Initial decouplings populated by --perf-op-mode flag.
    // Used for perf output dir path
    unordered_map<string, unordered_set<PerfTriscDecoupleMode>> initial_trisc_decouplings;
    // Specify the ops that will be decoupled from overlay and the type of decoupling
    unordered_map<string, PerfOverlayDecoupleMode> overlay_decouplings;
    // Decouplings that will get appended to after converting triplet settings to decouplings
    // Used for trisc compile
    unordered_map<string, unordered_set<PerfTriscDecoupleMode>> trisc_decouplings;
    // Used for setting up the testing infra. For every comparison config we execute a perf-check
    vector<PerfComparisonConfig> comparison_configs;
    // Overrides the performance targets if the target in comparison config is set to 0
    float cmdline_override_perf_target;
    // Whether to dump the debug-mailbox registers in a yaml
    bool dump_debug_mailbox = false;
    // Suppresses the warnings in the postprocessor
    bool suppress_warning_messages = false;
    // Input-indices that we record the performance for
    vector<string> target_inputs;
    // Graph-names that we record performance for
    set<string> target_epochs;
    // Override the output directory. Default: Test_output_dir / perf_results
    string override_perf_output_dir = "";

    bool measure_steady_state_perf = false;
    bool dump_perf_vcd = false;
    // Forces all the feeders to be MathPack decoupled and all the consumers of the op to be UnpMath decoupled
    vector<string> triplet_modes;
    // This is to accelerate compilation of successive runs of the same test with different decouplings.
    // We will only compile the ops that are specified under triplet_mode or reset_triplet_modes
    // And skip the compile for rest
    vector<string> reset_triplet_modes;
    // Skip performance check for all comparison configs
    bool skip_perf_check = false;
    // Generate the original perf_postprocess.cpp without shifting the numbers based on the alignment info
    // The numbers are shifted based on tt_cluster::record_device_runtime_start timestamps that we recorded
    bool generate_original_report = false;
    
    map<string, std::array<uint32_t, 3>> op_name_to_kernel_delay;
    vector<string> reset_input_output_delay;
    
    // The name of the graphs/queues to decouple
    vector<PerfDramDecouple> dram_decouple_config;

    bool append_device_runtime_to_host_report = false;
    bool reset_dram_perf_buffer = false;

    uint check_total_num_traces = 0;

    bool run_perf_analyzer = false;

    string perf_targets_dir;
    
    PerfDesc();
    PerfDesc(vector<string> &args, string netlist_path);

    bool is_normal_perf_mode() const;
    string get_decouple_mode_name() const;
    string get_overlay_decouple_string() const;
    
    // Returns the performance buffer size in l1 based on the perf-level and thread-id
    inline int32_t get_perf_buf_size(int thread_id) const {
        if (thread_id == 0 || thread_id == 2) {
            switch (perf_dump_level) {
                case 0: return l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0;
                case 1: return l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1;
                case 2: return l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1;
                default: log_assert(false, "Invalid perf_dump_level value.");
            }
        } else if (thread_id == 3) {
            switch (perf_dump_level) {
                case 0: return l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_0;
                case 1: return l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1;
                case 2: return l1_mem::address_map::NCRISC_PERF_BUF_SIZE_LEVEL_1;
                default: log_assert(false, "Invalid perf_dump_level value.");
            }
        } else if (thread_id == 1) {
            return l1_mem::address_map::MATH_PERF_BUF_SIZE;
        } else if (thread_id == 4) {
            return l1_mem::address_map::BRISC_PERF_BUF_SIZE;
        } else {
            log_assert(false, "Invalid thread_id.");
        }
        return 0;
    }

    // Must return the same value as thread_dump_size in pipegen.cpp
    // And CONCURRENT_PERF_BUF_SIZE in risc_perf.h
    inline int32_t get_host_thread_dump_size() {
        int32_t max_perf_buf_size = 0;
        for (int thread_id = 0; thread_id < l1_mem::address_map::PERF_NUM_THREADS; thread_id++) {
            int32_t thread_buf_size = get_perf_buf_size(thread_id);
            if (max_perf_buf_size < thread_buf_size) {
                max_perf_buf_size = thread_buf_size;
            }
        }
        if (perf_dump_level == 0) {
            log_assert(max_perf_buf_size == l1_mem::address_map::BRISC_PERF_BUF_SIZE,
                "Currently for concurrent mode, ncrisc assumes the largest buffer space in perf level 0 will be for brisc");
        } else {
            log_assert(max_perf_buf_size == l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1,
                "Currently for concurrent mode, ncrisc assumes the largest buffer space in perf level 0 will be for unpack/pack");
        }
        if (max_perf_buf_size == l1_mem::address_map::BRISC_PERF_BUF_SIZE ||
            max_perf_buf_size == l1_mem::address_map::MATH_PERF_BUF_SIZE) {
            return max_perf_buf_size;
        } else {
            return max_perf_buf_size / 2;
        }
    }

    const bool enabled() const {
        return (device_perf_mode != PerfDumpMode::Disable);
    }

    bool operator==(const struct PerfDesc& other) const {
        return (this->device_perf_mode == other.device_perf_mode)
            && (this->trisc_decouplings.size() == other.trisc_decouplings.size())
            && std::equal(this->trisc_decouplings.begin(), this->trisc_decouplings.end(), other.trisc_decouplings.begin())
            && (this->overlay_decouplings.size() == other.overlay_decouplings.size())
            && std::equal(this->overlay_decouplings.begin(), this->overlay_decouplings.end(), other.overlay_decouplings.begin())
            && (this->dram_decouple_config.size() == other.dram_decouple_config.size())
            && std::equal(this->dram_decouple_config.begin(), this->dram_decouple_config.end(), other.dram_decouple_config.begin())
            && (this->triplet_modes.size() == other.triplet_modes.size())
            && std::equal(this->triplet_modes.begin(), this->triplet_modes.end(), other.triplet_modes.begin())
            && (this->op_name_to_kernel_delay.size() == other.op_name_to_kernel_delay.size())
            && std::equal(this->op_name_to_kernel_delay.begin(), this->op_name_to_kernel_delay.end(), other.op_name_to_kernel_delay.begin());
    }

    inline bool always_compile() {
        return (
            !triplet_modes.empty() ||
            !reset_triplet_modes.empty() ||
            !op_name_to_kernel_delay.empty() ||
            !reset_input_output_delay.empty());
    }

    inline const bool skip_compile_for_op(const string &op_name) const {
        if (
            trisc_decouplings.find(op_name) == trisc_decouplings.end() &&
            op_name_to_kernel_delay.find(op_name) == op_name_to_kernel_delay.end() &&
            overlay_decouplings.find(op_name) == overlay_decouplings.end()) {
            
            return true;
        } else {
            return false;
        }
    }

    inline const bool is_perf_enabled_for_graph(string graph_name) const {
        bool perf_en = (
            device_perf_mode != perf::PerfDumpMode::Disable &&
                (target_epochs.size() == 0 || target_epochs.find(graph_name) != target_epochs.end()));
        return perf_en;
    }

    inline const bool is_dram_decouplings_en() const {
        return dram_decouple_config.size() > 0;
    }
};

inline uint64_t get_event_id(HostEventType event_type, uint device_id = 0, uint epoch_id = 0, uint program_id = 0, uint custom_label_idx = 0) {
    uint64_t event_id;
    event_id  = (uint(event_type) & 0xff);
    event_id |= (device_id & 0xff) << 8;
    event_id |= (epoch_id & 0xffff) << 16;
    event_id |= uint64_t(program_id & 0xff) << 32;
    event_id |= uint64_t(custom_label_idx & 0xffffff) << 40;
    return event_id;
}

inline uint get_ncrisc_event_type(int event_id) {
    return (event_id >> 24)  & 0xff;
}

inline uint64_t events_32b_to_64b(uint32_t event_32b_h, uint32_t event_32b_l) {
    return (uint64_t(event_32b_h) << 32) | event_32b_l;
}

void print_epoch_perf_table(const vector<EpochPerfInfo> &all_epochs, const string &output_path, bool steady_state);

inline uint64_t get_timestamp() {
    auto current_timestamp = LoaderClock::now();
    Duration duration = current_timestamp.time_since_epoch();
    uint64_t duration_ns = std::chrono::duration_cast<ns>(duration).count();
    return duration_ns;
}


////////////////////////////////////////////////////////
//////////// Backend Profiler
////////////////////////////////////////////////////////
struct tt_backend_perf {
private:
    const bool en = parse_env("TT_BACKEND_PROFILER", false);

public:
    vector<uint64_t> perf_buffer;
    vector<uint64_t> device_start_end_buffer;
    
    vector<string> all_labels;
    unordered_map<string, uint> label_to_index;
    
    string test_output_dir = "";
    string perf_output_dir = "";
    pid_t process_id = 0;
    int total_input_count = -1;
    set<int> active_profiler_threads;
    bool postprocessor_executed = false;

    tt_backend_perf() {}
    ~tt_backend_perf() {
        if (!postprocessor_executed) {
            finish_host_perf_profiler(true);
        }
        perf_buffer.clear();
        active_profiler_threads.clear();
        device_start_end_buffer.clear();
        all_labels.clear();
        label_to_index.clear();
    }
    
    const bool is_enabled() const {
        return en;
    }

    inline void record_device_start(uint64_t runtime_start, uint64_t device_start_cycle, uint device_id) {
        uint64_t host_event_id = get_event_id(perf::HostEventType::DEVICE_RUNTIME, device_id);
        uint64_t device_event_id = get_event_id(perf::HostEventType::DEVICE_START_CYCLE, device_id);
        uint64_t device_event_id_aligned = get_event_id(perf::HostEventType::DEVICE_START_CYCLE_ALIGNED, device_id);
        device_start_end_buffer.push_back(host_event_id);
        device_start_end_buffer.push_back(runtime_start);

        device_start_end_buffer.push_back(device_event_id);
        device_start_end_buffer.push_back(device_start_cycle);

        device_start_end_buffer.push_back(device_event_id_aligned);
        device_start_end_buffer.push_back(0);
    }

    inline void record_device_end(uint64_t runtime_end, uint64_t device_end_cycle, uint64_t device_end_cycle_aligned, uint device_id) {
        uint64_t host_event_id = get_event_id(perf::HostEventType::DEVICE_RUNTIME, device_id);
        uint64_t event_id_end = perf::get_event_id(perf::HostEventType::DEVICE_END_CYCLE, device_id);
        uint64_t event_id_end_aligned = perf::get_event_id(perf::HostEventType::DEVICE_END_CYCLE_ALIGNED, device_id);
        device_start_end_buffer.push_back(host_event_id);
        device_start_end_buffer.push_back(runtime_end);

        device_start_end_buffer.push_back(event_id_end);
        device_start_end_buffer.push_back(device_end_cycle);

        device_start_end_buffer.push_back(event_id_end_aligned);
        device_start_end_buffer.push_back(device_end_cycle_aligned);
    }

    template<BackendProfilerLevel level = BackendProfilerLevel::Info>
    inline void record_loader_event(const uint64_t &event_id, const uint64_t &event_value) {
        if constexpr (uint(level) <= uint(backend_profiler_level)) {
            if (en) {
                const std::lock_guard<std::mutex> lock(backend_profiler_mutex);
                perf_buffer.push_back(event_id);
                perf_buffer.push_back(event_value);
            }
        }
    }
    template<BackendProfilerLevel level = BackendProfilerLevel::Info>
    inline void record_loader_event(const uint64_t &event_id) {
        if constexpr (uint(level) <= uint(backend_profiler_level)) {
            if (en) {
                auto current_timestamp = LoaderClock::now();
                Duration duration = current_timestamp.time_since_epoch();
                uint64_t duration_ns = std::chrono::duration_cast<ns>(duration).count();
                record_loader_event(event_id, duration_ns);
            }
        }
    }
    template<BackendProfilerLevel level = BackendProfilerLevel::Info>
    inline void record_loader_event(const HostEventType &event_type, const uint device_id = 0, uint epoch_id = 0, uint program_id = 0) {
        if constexpr (uint(level) <= uint(backend_profiler_level)) {
            if (en) {
                uint64_t event_id = get_event_id(event_type, device_id, epoch_id, program_id);
                record_loader_event(event_id);
            }
        }
    }
    template<BackendProfilerLevel level = BackendProfilerLevel::Info>
    inline void record_loader_event(const string &event_label) {
        if constexpr (uint(level) <= uint(backend_profiler_level)) {
            if (en) {
                uint label_idx = check_and_insert_new_label(event_label);
                uint64_t event_id = get_event_id(HostEventType::CUSTOM, 0, 0, 0, label_idx);
                record_loader_event(event_id);
            }
        }
    }
    template<BackendProfilerLevel level = BackendProfilerLevel::Info>
    inline void record_loader_event(const string &event_label, const uint64_t &event_value) {
        if constexpr (uint(level) <= uint(backend_profiler_level)) {
            if (en) {
                const std::lock_guard<std::mutex> lock(backend_profiler_mutex);
                uint label_idx = check_and_insert_new_label(event_label);
                uint64_t event_id = get_event_id(HostEventType::CUSTOM, 0, 0, 0, label_idx);
                perf_buffer.push_back(event_id);
                perf_buffer.push_back(event_value);
            }
        }
    }
    inline uint check_and_insert_new_label(const string &event_label) {
        const std::lock_guard<std::mutex> lock(backend_profiler_mutex);
        log_assert(all_labels.size() < (uint64_t(1) << 24) - 1, "Number of custom host profiler events must be less than {}", (uint64_t(1) << 24) - 1);
        log_assert(en, "This api should only be called when backend profiler is enabled");
        uint label_idx;
        if (label_to_index.find(event_label) == label_to_index.end()) {
            label_idx = all_labels.size();
            label_to_index.insert({event_label, label_idx});
            all_labels.push_back(event_label);
        } else {
            label_idx = label_to_index.at(event_label);
        }
        return label_idx;
    }
    inline uint64_t get_current_timestamp() {
        auto current_timestamp = LoaderClock::now();
        Duration duration = current_timestamp.time_since_epoch();
        uint64_t duration_ns = std::chrono::duration_cast<ns>(duration).count();
        return duration_ns;
    }
    void dump_perf_buffer() {
        std::cout << "All perf events recorded for host runtime: " << std::endl;
        for (uint64_t event: perf_buffer) {
            std::cout << event << std::endl;
        }
    }
    void setup_host_perf_profiler(string in_test_output_dir, string in_perf_output_dir, uint in_pid, bool check_thread_exists);
    void finish_host_perf_profiler(bool called_by_destructor);
    void generate_device_alignment_report(string in_test_output_dir, string in_perf_output_dir, pid_t in_process_id);
    void set_total_input_count(int total_sample_count);

    inline bool all_threads_finished() {
        return active_profiler_threads.size() == 0;
    }
    inline bool last_thread_processing() {
        return active_profiler_threads.size() == 1;
    }
};

struct ScopedEventProfiler {
    private:
    std::function<void()> profiler_func;
    
    public:
    ScopedEventProfiler(const string &event_label);

    ScopedEventProfiler(const string &event_label, uint64_t event_value);

    ScopedEventProfiler(uint64_t event_id);

    ScopedEventProfiler(uint64_t event_id, uint64_t event_value);

    ScopedEventProfiler(const perf::HostEventType &event);

    ScopedEventProfiler(const perf::HostEventType &event, uint device_id);

    ScopedEventProfiler(const perf::HostEventType &event, uint device_id, uint epoch_id);

    ScopedEventProfiler(const perf::HostEventType &event, uint device_id, uint epoch_id, uint program_id);


    ~ScopedEventProfiler() {
        this->profiler_func();
    }
};

template <typename... Args>
inline void perf_warning(const PerfDesc &perf_desc, LogType type, char const* fmt, Args&&... args) {
    if (perf_desc.suppress_warning_messages) {
        return;
    }
    log_warning(type, fmt, args...);
}

} // namespace perf

template<>
struct std::hash<std::vector<perf::PerfDramDecouple> >
{
    std::size_t operator()(std::vector<perf::PerfDramDecouple> const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (auto& e : obj)
        {
            boost::hash_combine(hash_value, std::hash<perf::PerfDramDecouple>{}(e));
        }
        return hash_value;
    }
};

template<>
struct std::hash<std::unordered_set<perf::PerfTriscDecoupleMode> >
{
    std::size_t operator()(std::unordered_set<perf::PerfTriscDecoupleMode> const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (auto& v : obj)
        {
            boost::hash_combine(hash_value, std::hash<perf::PerfTriscDecoupleMode>{}(v));
        }
        return hash_value;
    }
};

template<>
struct std::hash<std::unordered_map<std::string, std::unordered_set<perf::PerfTriscDecoupleMode> > >
{
    std::size_t operator()(std::unordered_map<std::string, std::unordered_set<perf::PerfTriscDecoupleMode> > const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (auto& e : obj)
        {
            hash_value ^= std::hash<std::string>{}(e.first);
            boost::hash_combine(hash_value, std::hash<std::string>{}(e.first));
            for (auto& v : e.second)
            {
                boost::hash_combine(hash_value, std::hash<perf::PerfTriscDecoupleMode>{}(v));
            }
        }
        return hash_value;
    }
};

template<>
struct std::hash<std::unordered_map<std::string, perf::PerfOverlayDecoupleMode > >
{
    std::size_t operator()(std::unordered_map<std::string, perf::PerfOverlayDecoupleMode > const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (auto& e : obj)
        {
            hash_value ^= std::hash<std::string>{}(e.first);
            boost::hash_combine(hash_value, std::hash<std::string>{}(e.first));
            boost::hash_combine(hash_value, std::hash<perf::PerfOverlayDecoupleMode>{}(e.second));
        }
        return hash_value;
    }
};

template<>
struct std::hash<std::map<std::string, std::array<uint32_t, 3> > >
{
    std::size_t operator()(std::map<std::string, std::array<uint32_t, 3> > const& obj) const noexcept
    {
        std::size_t hash_value = 0;
        for (auto& e : obj)
        {
            hash_value ^= std::hash<std::string>{}(e.first);
            boost::hash_combine(hash_value, std::hash<std::string>{}(e.first));
            boost::hash_combine(hash_value, std::hash<uint32_t>{}(e.second.at(0)));
            boost::hash_combine(hash_value, std::hash<uint32_t>{}(e.second.at(1)));
            boost::hash_combine(hash_value, std::hash<uint32_t>{}(e.second.at(2)));
        }
        return hash_value;
    }
};

template<>
struct std::hash<perf::PerfDesc>
{
    std::size_t hash_for_unique_compile(perf::PerfDesc const& obj) const noexcept
    {
        std::size_t hash_value = std::hash<perf::PerfDumpMode>{}(obj.device_perf_mode);
        boost::hash_combine(hash_value, std::hash<uint32_t>{}(obj.perf_dump_level));
        boost::hash_combine(hash_value, std::hash<std::unordered_map<std::string, std::unordered_set<perf::PerfTriscDecoupleMode> > >{}(obj.trisc_decouplings));
        boost::hash_combine(hash_value, std::hash<std::unordered_map<std::string, perf::PerfOverlayDecoupleMode > >{}(obj.overlay_decouplings));
        boost::hash_combine(hash_value, std::hash<std::vector<perf::PerfDramDecouple> >{}(obj.dram_decouple_config));
        boost::hash_combine(hash_value, std::hash<std::map<string, std::array<uint32_t, 3>>> {}(obj.op_name_to_kernel_delay));
        for (auto &input: obj.target_inputs) {
            boost::hash_combine(hash_value, std::hash<string>{}(input));
        }
        for (auto& e : obj.comparison_configs)
        {
            boost::hash_combine(hash_value, std::hash<perf::PerfComparisonConfig>{}.hash_for_unique_compile(e));
        }
        boost::hash_combine(hash_value, std::hash<bool>{}(obj.dump_debug_mailbox ? "true" : "false" ));
        return hash_value;
    }
};
