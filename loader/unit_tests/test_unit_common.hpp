// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/buda_soc_descriptor.h"
#include "device/tt_cluster_descriptor.h"
#include "common/model/test_common.hpp"
#include "common/param_lib.hpp"

#include "gtest/gtest.h"

#include "loader/epoch_loader.hpp"
#include "loader/tt_cluster.hpp"
#include "tt_backend_api.hpp"

#include "tt_backend_api.hpp"

#include "utils/logger.hpp"

// Suppress stdout from an arbitrary func that may be too chatty
//
template <typename Func>
static inline auto quiet_call(Func func) {
    static bool verbose = testing::internal::BoolFromGTestEnv("verbose", false);  // GTEST_VERBOSE envvar
    // Only one stdout capturer can exist at a time, use lock to prevent multi-threaded access
    static std::mutex capturer_mutex;
    // Protect the capturer and capture stdout
    std::lock_guard<std::mutex> lock(capturer_mutex);
    testing::internal::CaptureStdout();
    auto ret_val = func();
    auto output = testing::internal::GetCapturedStdout();
    if (verbose) {
        std::cout << output;
    }
    return ret_val;
}

// Helper for providing test home
//
inline std::string test_path() {
    return tt::buda_home() + "/loader/unit_tests/";
}

// Implementation of a query with unspecified descriptors by default and returns casted result in desired type
// note that params_file is only for save/load params from file, no action performed if it is unspecified
template <typename T>
T get_param(
    const std::string& key,
    const std::string& sdesc_path = "",
    const std::string& ndesc_path = "",
    int device_id = -1,
    const std::string& params_file = "",
    const bool params_save = false) {

    std::string value = tt::backend::get_backend_param(key, sdesc_path, ndesc_path, params_file, params_save);

    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<T, int>) {
        return std::stoi(value, 0, 0);
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        return std::stoul(value, 0, 0);
    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
        return std::stoull(value, 0, 0);
    } else if constexpr (std::is_same_v<T, bool>) {
        return static_cast<bool>(std::stoi(value, 0, 0));
    } else if constexpr (std::is_same_v<T, tt_xy_pair>) {
        auto delimeter = value.find("-");
        auto x_str = value.substr(0, delimeter);
        auto y_str = value.substr(delimeter + 1, std::string::npos);
        return tt_xy_pair(std::stoi(x_str, 0, 0), std::stoi(y_str, 0, 0));
    } else {
        static_assert(tt::false_type_t<T>, "No specialization for type");
    }
};

// // Explicit instantiations
template std::string get_param<std::string>(std::string const &, std::string const &, std::string const &, int, std::string const &, bool const);
template int get_param<int>(std::string const &, std::string const &, std::string const &, int, std::string const &, bool const);
template std::uint32_t get_param<std::uint32_t>(std::string const &, std::string const &, std::string const &, int, std::string const &, bool const);
template std::uint64_t get_param<std::uint64_t>(std::string const &, std::string const &, std::string const &, int, std::string const &, bool const);
template bool get_param<bool>(std::string const &, std::string const &, std::string const &, int, std::string const &, bool const);
template tt_xy_pair get_param<tt_xy_pair>(std::string const &, std::string const &, std::string const &, int, std::string const &, bool const);

// Mimic the production behavior of device popping queues
//
inline void pop_epoch_queue(tt_epoch_queue &eq, tt_cluster *cluster) {
    tt_queue_ptr dram_ptr(eq.get_num_slots());
    tt_target_dram dram = {eq.d_chip_id, eq.d_chan, eq.d_subchannel};
    cluster->read_dram_vec(dram_ptr.ptrs, dram, eq.d_addr, 8);
    ASSERT_FALSE(dram_ptr.empty());
    dram_ptr.incr_rd();
    std::vector<uint32_t> ptr_vec = {dram_ptr.rd_ptr};
    cluster->write_dram_vec(ptr_vec, dram, eq.d_addr);
}

// Helper for creating a cluster and starting it
//
inline std::unique_ptr<tt_cluster> get_cluster(tt::ARCH arch, tt::TargetDevice target, const std::set<chip_id_t> &chip_ids = {}, const std::string &sdesc_in = "") {
    auto cluster = std::make_unique<tt_cluster>();
    std::string sdesc_path = sdesc_in;
    if (sdesc_path == "") {
        setenv("FORCE_FULL_SOC_DESC", "1", 1);
        sdesc_path = get_soc_description_file(arch, target, "");
    }
    cluster->open_device(arch, target, chip_ids, sdesc_path);
    cluster->start_device({.init_device = true});
    return cluster;
}

// Helper for querying current machine's arch
// returns invalid if current machine is not a silicon machine
inline tt::ARCH get_machine_arch() {
    auto machine_arch = tt::ARCH::Invalid;  // current machine is not a silicon machine
    if (get_param<int>("system-device-num_mmio_devices") > 0) {
        machine_arch = get_arch_name(get_param<std::string>("system-device0-type")); // assume all mmio devices are of the same arch
    }
    return machine_arch;
}