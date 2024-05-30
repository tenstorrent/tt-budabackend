// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <unordered_map>
#include <vector>
#include <string>

#include "utils/logger.hpp"
#include "common/buda_soc_descriptor.h"
#include "perf_lib/perf_base.hpp"

namespace perf {

using std::unordered_map;
using std::string;
using std::vector;

constexpr int buffer_size_undefined = -1;

const string overlay_buffer_name = "OVERLAY_BLOB";

// Keep consistent with extra_overlay_blob_l1_buffer.cpp
const string extra_overlay_buffer_name = "Extra overlay blob buffer";

/**************************
 * L1 Profiler Structures
***************************/
enum class L1BufferType {
    BinaryBuffer,
    DataBuffer,
};

enum class L1ProfileStage {
    Uninitialized = 0,
    Initialized = 1,
    ReservedBinaries = 2,
    Pipegen = 3,
    ActualBinaries = 4,
    Done = 5
};

class L1Buffer {
    private:
    L1BufferType m_buffer_type;
    string m_name;
    uint32_t m_start_addr;

    // consumed size can be negative
    // which indicates we don't know how much is consumed
    int m_consumed_size;

    // this is the size that was reserved for this buffer
    // reserved size can be negative if we don't know how much is reserved
    int m_reserved_size;

    public:
    L1Buffer() = default;
    L1Buffer(const L1BufferType &buffer_type, const string &name, uint32_t start_addr, int consumed_size, int reserved_size):
        m_buffer_type(buffer_type), m_name(name), m_start_addr(start_addr), m_consumed_size(consumed_size), m_reserved_size(reserved_size) {
        if (reserved_size != buffer_size_undefined) {
            log_assert(consumed_size <= reserved_size, "L1Buffer consumes {} bytes but only reserves {} bytes", consumed_size, reserved_size);
        }
    }

    void update_consumed_size(int new_consumed_size);
    void update_reserved_size(int new_reserved_size);

    inline const string &name() const {
        return m_name;
    }

    inline string category_name() const {
        if (is_binary_buffer()) {
            return "binary-buffers";
        }
        else if (is_data_buffer()) {
            return "data-buffers";
        }
        else {
            log_fatal("Invalid buffer type");
        }
    }

    inline int c_size() const {
        return m_consumed_size;
    }

    inline int r_size() const {
        return m_reserved_size;
    }

    inline bool has_reserved_size() const {
        return m_reserved_size != buffer_size_undefined;
    }

    inline bool has_consumed_size() const {
        return m_consumed_size != buffer_size_undefined;
    }

    inline uint32_t start_addr() const {
        return m_start_addr;
    }

    inline uint32_t end_addr() const {
        if (has_reserved_size()) {
            return m_start_addr + m_reserved_size;
        }
        else if (has_consumed_size()) {
            return m_start_addr + m_consumed_size;
        }
        return m_start_addr;
    }

    inline bool is_binary_buffer() const {
        return m_buffer_type == L1BufferType::BinaryBuffer;
    }

    inline bool is_data_buffer() const {
        return m_buffer_type == L1BufferType::DataBuffer;
    }

    inline float get_percent_consumed() const {
        return float(m_consumed_size) / float(m_reserved_size) * 100;
    }

};

class L1Core {
    private:
    const chip_id_t m_device_id = -1;
    const tt_xy_pair m_physical_coord = tt_xy_pair();
    const tt_xy_pair m_logical_coord = tt_xy_pair();
    const uint32_t m_l1_size = 0;
    const string m_op_name = "";
    const string m_op_type = "";
    uint32_t m_total_consumed_size = 0;
    uint32_t m_total_reserved_size = 0;
    // contains all the buffers in the core's l1
    vector<std::shared_ptr<L1Buffer>> m_all_buffers;
    // maps start address to buffer, points to the same buffers as m_all_buffers
    unordered_map<uint32_t, std::shared_ptr<L1Buffer>> m_buffer_address_map;

    public:
    L1Core() = default;
    L1Core(const tt_cxy_pair &physical_coord, const tt_cxy_pair &logical_coord, const string &op_name, const string &op_type, uint32_t l1_size);

    void add_buffer(const L1BufferType &buffer_type, const string &name, uint32_t start_addr, int consumed_size, int reserved_size);

    void update_buffer_consumed_size(uint32_t start_addr, int new_consumed_size);
    void update_buffer_reserved_size(uint32_t start_addr, int new_reserved_size);

    void sort_buffers_and_check_overlap();

    const L1Buffer* find_buffer(uint32_t start_addr) const;

    vector<const L1Buffer*> get_all_buffers() const;

    inline const tt_xy_pair &physical_coord() const { 
        return m_physical_coord; 
    }

    inline const tt_xy_pair &logical_coord() const { 
        return m_logical_coord; 
    }

    inline const chip_id_t &device() const {
        return m_device_id;
    }

    inline const string &op_name() const {
        return m_op_name;
    }

    inline const string &op_type() const {
        return m_op_type;
    }

    inline int consumed_size_bytes() const {
        return m_total_consumed_size;
    }

    inline int reserved_size_bytes() const {
        return m_total_reserved_size;
    }

    inline float get_percent_consumed_of_reserved() const {
        return float(m_total_consumed_size) / float(m_total_reserved_size) * 100;
    }

    inline float get_percent_reserved_of_l1() const {
        return float(m_total_reserved_size) / float(m_l1_size) * 100;
    }

    inline float get_percent_consumed_of_l1() const {
        return float(m_total_consumed_size) / float(m_l1_size) * 100;
    }

    inline uint32_t get_l1_unreserved_size() const {
        return m_l1_size - m_total_reserved_size;
    }

    inline uint32_t get_l1_unconsumed_size() const {
        return m_l1_size - m_total_consumed_size;
    }

};

// An L1 graph should contain a unordered_set of L1 cores
class L1Graph {
    private:
    const int m_temporal_epoch = -1;
    const string m_graph_name = "";
    const chip_id_t m_target_device = -1;

    // these two maps point to the same objects
    unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> m_physical_l1_cores;
    unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> m_logical_l1_cores;

    // this flag indicates if all l1 buffer sizes have been recorded for this graph.
    L1ProfileStage m_profile_stage = L1ProfileStage::Uninitialized;

    public:
    L1Graph() = default;
    L1Graph(const string &graph_name, int temporal_epoch, chip_id_t target_device, const unordered_map<string, core_descriptor> &cores_to_descriptors, uint32_t l1_size);
    
    void add_buffer_to_core(
        const tt_cxy_pair &core, 
        const L1BufferType &buffer_type, 
        const string &name,
        uint32_t start_addr, 
        int consumed_size, 
        int reserved_size,
        CoordType coord_type
    );

    void update_core_buffer_consumed_size(const tt_cxy_pair &core, uint32_t start_addr, int new_consumed_size, CoordType coord_type);

    void update_buffer_consumed_size_of_all_used_workers(uint32_t start_addr, int new_consumed_size);

    void update_core_buffer_reserved_size(const tt_cxy_pair &core, uint32_t start_addr, int new_reserved_size, CoordType coord_type); 

    void add_buffer_to_all_used_workers(const L1BufferType &buffer_type, const string &name, uint32_t start_addr, int consumed_size, int reserved_size);
    

    void sort_buffers_and_check_overlap();

    const L1Buffer* find_buffer_in_core(const tt_cxy_pair &core, CoordType coord_type, uint32_t start_addr) const;

    inline int temporal_epoch() const {
        return m_temporal_epoch;
    }
    
    inline const chip_id_t &target_device() const {
        return m_target_device;
    }

    inline const L1ProfileStage &get_profile_stage() const {
        return m_profile_stage;
    }
    inline void set_profile_stage(L1ProfileStage stage) {
        m_profile_stage = stage;
    }

    inline const string &graph_name() const {
        return m_graph_name;
    }

    unordered_map<tt_xy_pair, std::shared_ptr<L1Core>> &get_cores_by_coord_type(CoordType coord_type);
};

class L1Profiler {
    private:
    const string m_arch_name = "";
    const uint32_t m_l1_size = 0;
    // maps graph name to the binaries allocated to it
    unordered_map<string, std::unique_ptr<L1Graph>> m_graphs;

    public:
    L1Profiler() = default;
    L1Profiler(const string &arch_name, uint32_t l1_size): m_arch_name(arch_name), m_l1_size(l1_size) {};

    void add_graph(const string &graph_name, int temporal_epoch, chip_id_t target_device, const unordered_map<string, core_descriptor> &cores_to_descriptors);

    void add_buffer_to_graph(
        const string& graph_name, 
        const tt_cxy_pair &core_coord, 
        const L1BufferType &buffer_type, 
        const string &buffer_name, 
        uint32_t start_addr, 
        int consumed_size, 
        int reserved_size,
        CoordType coord_type,
        L1ProfileStage stage
    );

    void update_graph_buffer_consumed_size(
        const string &graph_name, 
        const tt_cxy_pair &core_coord, 
        uint32_t start_addr, 
        int new_consumed_size, 
        CoordType coord_type,
        L1ProfileStage stage
    );

    void broadcast_update_buffer_consumed_size_of_graph(const string &graph_name, uint32_t start_addr, int new_consumed_size, L1ProfileStage stage);

    void update_graph_buffer_reserved_size(
        const string &graph_name, 
        const tt_cxy_pair &core_coord, 
        uint32_t start_addr, 
        int new_reserved_size, 
        CoordType coord_type,
        L1ProfileStage stage
    );

    void broadcast_add_buffer_all_used_workers(const string &graph_name, const L1BufferType &buffer_type, const string &buffer_name, uint32_t start_addr, int consumed_size, int reserved_size, L1ProfileStage stage);

    void set_graph_profile_stage(const string &graph_name, L1ProfileStage stage);

    L1ProfileStage get_graph_stage(const string &graph_name) const;

    void sort_graph_buffers_and_check_overlap(const string &graph_name);

    void sort_all_graph_buffers_and_check_overlap();

    const L1Buffer* find_buffer_in_graph(const string &graph_name, const tt_cxy_pair &core, CoordType coord_type, uint32_t start_addr) const;

    void create_reports(const string &output_dir) const;
};

/*****************************
 * Memory Profiler Structures  
******************************/

/* 
Memory profiler is a wrapper struct that contains metadata of graphs, device descriptors etc.
Profilers of different memory units (l1, dram) are to be wrapped in MemoryProfiler.
*/
class MemoryProfiler {
    private:
    const bool m_profile_l1 = false;
    string m_arch_name = "";
    uint32_t m_l1_size = 0;
    unordered_map<chip_id_t, std::shared_ptr<buda_SocDescriptor>> m_chip_id_to_sdesc;

    unordered_map<string, std::shared_ptr<tt_perf_digraph_wrapper>> m_graph_wrappers_graph_name;
    unordered_map<string, std::unordered_set<tt_xy_pair>> m_graph_logical_used_workers;
    unordered_map<string, std::unordered_set<tt_xy_pair>> m_graph_physical_used_workers;
    unordered_map<int, unordered_map<chip_id_t, std::shared_ptr<tt_perf_digraph_wrapper>>> m_graph_wrappers_temporal_epoch_device;
    
    std::unique_ptr<L1Profiler> m_l1_profiler;
    std::recursive_mutex m_l1_profiler_mutex;

    public:
    MemoryProfiler() = default;
    MemoryProfiler(const unordered_map<chip_id_t, buda_SocDescriptor> &sdesc_per_chip, bool l1_profile_en);

    // try to add new graph to profiler, potentially adding new devices and their soc descriptors
    void add_graph(const buda_SocDescriptor &sdesc, const tt_digraph &graph, int temporal_epoch_id);

    // update device soc descriptors
    void update_chip_id_to_sdesc(chip_id_t chip_id, const buda_SocDescriptor &soc_descriptor);

    // update graph wrapper info for new graph
    void update_graph_wrappers(const string &graph_name, const tt_digraph &graph, int temporal_epoch_id);

    // update graph used worker cores (cores than ran tensix ops)
    void update_graph_used_workers(const string &graph_name, const unordered_map<string, core_descriptor> &cores_to_descriptors);

    inline bool temporal_epoch_device_exists(int temporal_epoch_id, chip_id_t device_id) const {
        return m_graph_wrappers_temporal_epoch_device.find(temporal_epoch_id) != m_graph_wrappers_temporal_epoch_device.end() and
            m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).find(device_id) != m_graph_wrappers_temporal_epoch_device.at(temporal_epoch_id).end();
    }

    const string &get_graph_name(int temporal_epoch_id, chip_id_t device_id) const;

    bool is_used_worker(int temporal_epoch_id, const tt_cxy_pair &core, CoordType coord_type) const;

    bool is_used_worker(const string &graph_name,const tt_xy_pair &core, CoordType coord_type) const;

    // create l1 profiler reports
    void create_reports_l1(const string &output_dir) const;

    // add buffer to a core's l1
    void add_buffer_to_graph_l1(
        const string &graph_name, 
        const tt_cxy_pair &core_coord, 
        const L1BufferType &buffer_type, 
        const string &buffer_name, 
        uint32_t start_addr, 
        int consumed_size, 
        int reserved_size,
        CoordType coord_type,
        L1ProfileStage stage
    ); 

    // add buffer to a core's l1, graph found based on temporal epoch id and device id of core_coord
    void add_buffer_to_graph_l1(
        int temporal_epoch_id, 
        const tt_cxy_pair &core_coord, 
        const L1BufferType &buffer_type, 
        const string &buffer_name, 
        uint32_t start_addr, 
        int consumed_size, 
        int reserved_size,
        CoordType coord_type,
        L1ProfileStage stage
    );

    // add buffer to every worker core's l1 in every graph
    void broadcast_add_buffer_l1(const L1BufferType &buffer_type, const string &buffer_name, uint32_t start_addr, int consumed_size, int reserved_size, L1ProfileStage stage);

    // Update the consumed size of buffer. Buffers are found based on start_addr
    void update_buffer_consumed_size_l1(
        const string &graph_name, 
        const tt_cxy_pair &core_coord, 
        uint32_t start_addr, 
        int new_consumed_size, 
        CoordType coord_type,
        L1ProfileStage stage
    );

    // Update consumed size of every buffer in every graph.
    void broadcast_update_buffer_consumed_size_l1(
        uint32_t start_addr,
        int new_consumed_size,
        L1ProfileStage stage
    );

    void update_buffer_reserved_size_l1(
        const string &graph_name, 
        const tt_cxy_pair &core_coord, 
        uint32_t start_addr, 
        int new_reserved_size, 
        CoordType coord_type,
        L1ProfileStage stage
    );
    
    void update_graph_profile_stage_l1(const string &graph_name, L1ProfileStage stage, bool force_update = false);

    void update_temporal_epoch_profile_stage_l1(int temporal_epoch_id, L1ProfileStage stage, bool force_update = false);

    void update_profile_stage_all_graphs_l1(L1ProfileStage stage, bool force_update = false);

    L1ProfileStage get_graph_profile_stage_l1(const string &graph_name);

    L1ProfileStage get_graph_profile_stage_l1(int temporal_epoch_id, chip_id_t device_id);

    void sort_graph_buffers_and_check_overlap_l1(const string &graph_name);

    void sort_all_graph_buffers_and_check_overlap_l1();
    
    const L1Buffer* find_buffer_in_graph_l1(const string &graph_name, const tt_cxy_pair &core, CoordType coord_type, uint32_t start_addr);

    // check if the l1 profiler is enabled
    inline bool profile_l1() const {
        return m_profile_l1;
    }

};

}
