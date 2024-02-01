// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <unistd.h>

#include "device/tt_cluster_descriptor.h" // For chip_id_t

#include "fmt/ranges.h" // For logger printing of vectors
#include "hwloc.h"

using tt_cluster_description = tt_ClusterDescriptor;

namespace tt {
//! Utility functions for various backend paramsf
namespace cpuset {

int get_allowed_num_threads();

// CPU ID allocator for pinning threads to cpu_ids
// It's a singleton that should be retrieved via get()
struct tt_cpuset_allocator {
    public:

        tt_cpuset_allocator(tt_cpuset_allocator const&)     = delete;
        void operator=(tt_cpuset_allocator const&)          = delete;

        static void bind_thread_to_cpuset(tt_cluster_description *ndesc, chip_id_t device_id, bool skip_singlify=false){
            auto& instance = tt_cpuset_allocator::get();
            instance.bind_thread_cpuset(ndesc, device_id, skip_singlify);
        }
        
        static void unbind_thread_from_cpuset(){
            auto& instance = tt_cpuset_allocator::get();
            instance.unbind_thread_cpuset();
        }

        static void clear_state_and_cpuset_pins(){
            auto& instance = tt_cpuset_allocator::get();
            instance.clear_state();
        }

        // Bind an already allocated memory region to particular numa nodes
        static bool bind_area_to_memory_nodeset(chip_id_t physical_device_id, const void * addr, size_t len){
            auto& instance = tt_cpuset_allocator::get();
            return instance.bind_area_memory_nodeset(physical_device_id, addr, len);
        }

        // Store process' main thread_id (not required, mainly for checking purposes to ensure no cpubinds on it occur).
        static void set_main_thread_id(){
            auto& instance = tt_cpuset_allocator::get();
            instance._set_main_thread_id();
        }

        static int get_num_cpu_cores_allocated_to_device(chip_id_t physical_device_id){
            auto& instance = tt_cpuset_allocator::get();
            auto num_cores = instance.m_enable_cpuset_allocator ? instance.m_num_cpu_cores_allocated_per_tt_device.at(physical_device_id) : get_allowed_num_threads();
            return num_cores;
        }

        static int get_num_tt_pci_devices(){
            auto& instance = tt_cpuset_allocator::get();
            return instance._get_num_tt_pci_devices();
        }

        static int get_num_tt_pci_devices_by_pci_device_id(uint16_t device_id, uint16_t revision_id){
            auto& instance = tt_cpuset_allocator::get();
            return instance._get_num_tt_pci_devices_by_pci_device_id(device_id, revision_id);
        }

    private:

        static tt_cpuset_allocator& get() {
            static tt_cpuset_allocator instance;
            return instance;
        }

        tt_cpuset_allocator();

        int TENSTORRENT_VENDOR_ID = 0x1e52;

        void bind_thread_cpuset(tt_cluster_description *ndesc, chip_id_t device_id, bool skip_singlify);
        void unbind_thread_cpuset();
        void store_thread_original_cpuset();
        bool bind_area_memory_nodeset(chip_id_t physical_device_id, const void * addr, size_t len);
        void _set_main_thread_id();
        int _get_num_tt_pci_devices();
        int _get_num_tt_pci_devices_by_pci_device_id(uint16_t device_id, uint16_t revision_id);

        void clear_state();
        hwloc_cpuset_t allocate_cpu_set_for_thread(chip_id_t physical_device_id, bool skip_singlify);

        // Series of init functions, must be called in this order. Seperated out to support
        // early exit in case of errors.
        bool init_topology_init_and_load();
        bool init_find_tt_pci_devices_packages_numanodes();
        bool init_get_number_of_packages();
        bool init_is_cpu_model_supported();
        bool init_determine_cpuset_allocations();
        bool init_populate_physical_mmio_device_id_map();

        // Helper Functions
        std::string get_pci_bus_id(hwloc_obj_t pci_device_obj);
        int get_package_id_from_device(hwloc_obj_t pci_device_obj, chip_id_t physical_device_id);
        hwloc_nodeset_t get_numa_nodeset_from_device(hwloc_obj_t pci_device_obj, chip_id_t physical_device_id);

        // Debug Functions
        void print_hwloc_cpuset(hwloc_obj_t &obj);
        void print_hwloc_nodeset(hwloc_obj_t &obj);
        void print_hwloc_object(hwloc_obj_t &obj, int depth = 0, bool verbose = false, bool show_cpuids = true);
        std::vector<int> get_hwloc_bitmap_vector(hwloc_bitmap_t &bitmap);
        std::vector<int> get_hwloc_cpuset_vector(hwloc_obj_t &obj);
        std::vector<int> get_hwloc_nodeset_vector(hwloc_obj_t &obj);
        hwloc_topology_t m_topology;
        bool m_debug;
        bool m_skip_singlify;
        pid_t m_pid;

        std::map<chip_id_t, chip_id_t> m_logical_to_physical_mmio_device_id_map;

        // Items calculated by parsing system info, used by allocation algorithm:
        std::map<int, std::vector<int>> m_package_id_to_devices_map;
        std::map<int, std::string> m_physical_device_id_to_pci_bus_id_map; // Debug/Info
        std::map<std::pair<uint16_t, uint16_t>, int> m_num_tt_device_by_pci_device_id_map;

        std::map<chip_id_t, std::vector<hwloc_cpuset_t>> m_physical_device_id_to_cpusets_map;
        std::map<chip_id_t, int> m_physical_device_id_to_package_id_map;

        std::mutex allocate_cpu_id_mutex;

        bool m_enable_cpuset_allocator = true; // Enable feature, otherwise do nothing.
        int m_num_packages = 0;
        std::vector<int> m_all_tt_devices = {};

        hwloc_obj_type_t m_object_per_alloc_slot = HWLOC_OBJ_L3CACHE; // Default


        // For 2CCX-PER-CCD Optimization detection.
        std::map<int, int> m_package_id_to_num_l3_per_ccx_map;
        std::map<int, int> m_package_id_to_num_ccx_per_ccd_map;

        std::map<chip_id_t, int> m_num_threads_pinned_per_tt_device;
        std::unordered_set<std::thread::id> m_global_thread_ids_pinned = {};
        std::thread::id m_main_thread_id;
        bool m_stored_main_thread_id = false;

        // For quicker unbinding of threads, record the physical_device_id during binding.
        std::map<std::thread::id, chip_id_t> m_global_thread_id_to_physical_device_id_map = {};

        // For storing original cpuset during binding, to restore during unbinding.
        std::map<std::thread::id, hwloc_cpuset_t> m_global_thread_id_to_original_cpuset_map = {};

        // Memory Binding
        std::map<chip_id_t, hwloc_nodeset_t> m_physical_device_id_to_numa_nodeset_map;

        // Helper for some dynamic multi-threading.
        std::map<chip_id_t, int> m_num_cpu_cores_allocated_per_tt_device;

};

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
    os << "{";
    bool first = true;
    for (T const &elem : v) {
        if (!first) {
            os << ", ";
        }
        os << elem;
        first = false;
    }
    os << "}";
    return os;
}

}  // namespace cpuset
}  // namespace tt
