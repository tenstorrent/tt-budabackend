// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "compile_trisc/compile_trisc.hpp"

#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>

#include "common/cache_lib.hpp"
#include "common/model/tt_core.hpp"
#include "common/tt_parallel_for.h"
#include "common/env_lib.hpp"
#include "common/size_lib.hpp"
#include "device/cpuset_lib.hpp"
#include "epoch_q.h"
#include "netlist_utils.hpp"
#include "perf_lib/postprocess.hpp"
#include "utils/logger.hpp"
#include "utils/scoped_timer.hpp"
#include "runtime/runtime_utils.hpp"
#include "net2hlks_lib/net2hlks.h"

namespace fs = std::experimental::filesystem;  // see comment above
namespace tt {

void generate_all_fw(
    const netlist_workload_data& workload,
    string device_name,
    perf::PerfDesc& perf_desc,
    bool compile_for_perf_only,
    string build_dir_path) {
    PROFILE_SCOPE_MS();

    int num_threads = tt::cpuset::get_allowed_num_threads();

    log_debug(tt::LogCompileTrisc, "generate_all_fw() -- num_threads: {}", num_threads);

    if (!compile_for_perf_only) {
        generate_brisc_fw(perf_desc, device_name, build_dir_path);
        generate_ncrisc_fw(perf_desc, device_name, build_dir_path);
        if (device_name == "wormhole" || device_name == "wormhole_b0" || device_name == "blackhole") {
            generate_erisc_fw(perf_desc, device_name, build_dir_path);
        }
    }
    generate_trisc_fw(
        workload, device_name, perf_desc, compile_for_perf_only, build_dir_path, num_threads);
}

std::string get_graph_path_prefix(const std::string& build_dir_path) { return build_dir_path + "/graph_"; }

std::string get_graph_path(const std::string& build_dir_path, const std::string& graph_name) {
    return get_graph_path_prefix(build_dir_path) + graph_name;
}

std::string get_op_path(const std::string& graph_path, const int op_index) {
    return graph_path + "/op_" + to_string(op_index);
}

void remove_dirs_with_prefix(const std::string& prefix, int num_threads) {
    const std::string absoulte_prefix = fs::absolute(prefix);
    fs::path parent_path = fs::path(absoulte_prefix).parent_path();
    std::string folder_prefix = fs::path(absoulte_prefix).filename().string();
    std::vector<fs::path> entries_to_delete;
    if (fs::exists(parent_path) && fs::is_directory(parent_path)) {
        for (const auto& entry : fs::directory_iterator(parent_path)) {
            entries_to_delete.emplace_back(entry.path());
        }
    }

    tt::parallel_for(
        entries_to_delete.begin(),
        entries_to_delete.end(),
        [&](const auto& entry) {
            if (fs::is_directory(entry)) {
                std::string folder_name = entry.filename().string();
                if (folder_name.compare(0, folder_prefix.size(), folder_prefix) == 0) {
                    log_debug(tt::LogCompileTrisc, "Deleting existing directory: {}", entry);
                    fs::remove_all(entry);
                }
            }
        },
        num_threads);
}

void remove_graph_build_dirs(const std::string& build_dir_path, int num_threads) {
    remove_dirs_with_prefix(get_graph_path_prefix(build_dir_path), num_threads);
}

void force_create_build_dir(const std::string& output_dir) {
    if (fs::exists(output_dir)) {
        log_debug(tt::LogCompileTrisc, "Deleting existing build output_dir: {}", output_dir);
        fs::remove_all(output_dir);
    }
    fs::create_directory(output_dir);
}

void get_tt_log_and_tt_llk_dump(int& tt_log_out, int& tt_llk_dump_out) {
    tt_llk_dump_out = parse_env("ENABLE_TT_LLK_DUMP", 0);

    string enable_tt_log_str = parse_env<string>("ENABLE_TT_LOG", "");

    #if defined(TT_DEBUG)
        // In debug builds, we want TT_LOG enabled by default, unless explititly
        // disabled by the writing ENABLE_TT_LOG=0 env variable
        if (enable_tt_log_str != "0") {
            tt_log_out = 1;
            return;
        }
    #endif

    // Otherwise, tt_log is enabled if user set it active.
    tt_log_out = enable_tt_log_str.size() != 0 && enable_tt_log_str != "0";
}

void generate_brisc_fw(const perf::PerfDesc& perf_desc, const std::string& arch_name, const std::string& out_dir_path) {
    PROFILE_SCOPE_MS();
    fs::path build_dir(out_dir_path);
    log_assert(fs::exists(build_dir), "Error: out_dir_path doesn't exist");

    stringstream make_clean_cmd;
    stringstream make_cmd;
    string root = buda_home(fs::current_path().string());

    bool is_perf_dump_en = perf_desc.device_perf_mode != perf::PerfDumpMode::Disable;
    bool is_perf_spill_dram = perf_desc.device_perf_mode == perf::PerfDumpMode::IntermediateDump;
    bool is_concurrent_perf = perf_desc.device_perf_mode == perf::PerfDumpMode::Concurrent;
    uint32_t perf_dump_level = perf_desc.perf_dump_level;
    bool is_overlay_decoupled = is_perf_dump_en && perf_desc.overlay_decouplings.size() > 0;

    bool is_default_brisc_bin = !is_perf_dump_en && (perf_dump_level == 0) && !is_overlay_decoupled;

    string brisc_out_dir = fs::absolute(out_dir_path).string() + "/brisc";
    string log_file = fs::absolute(out_dir_path).string() + "/brisc_build.log";
    force_create_build_dir(brisc_out_dir);

    string blob_init_build_dir = out_dir_path + "/blob_init";
    force_create_build_dir(blob_init_build_dir);
    fs::path brisc_build_path(root + "build/src/firmware/riscv/targets/brisc/out/");

    if (is_default_brisc_bin && fs::exists(brisc_build_path)) {
        fs::path brisc_fw_path(brisc_build_path.string() + "/brisc.hex");
        log_assert(fs::exists(brisc_fw_path), "Error {} doesn't exist", brisc_fw_path);
        log_debug(tt::LogCompileTrisc,"Using Default BRISC Bin");
        fs::copy(brisc_build_path, brisc_out_dir);
    } else {
        bool loaded_brisc_bin = false;
        // If set, the variable stores the directory of the compiled brisc binaries for a specific configuration;
        // It is up to the user to make sure the brisc configuration used for compilation is the right one.
        // This is useful in perf_sweep.py where we run large number of tests with the same perf configuration.
        if (std::getenv("BRISC_BIN_CACHE_DIR") != nullptr) {
            loaded_brisc_bin = try_load_fw_bin("brisc", std::getenv("BRISC_BIN_CACHE_DIR"), root, brisc_out_dir);
        }

        if (!loaded_brisc_bin) {
            log_debug(tt::LogCompileTrisc, "Recompiling BRISC firmware");
            std::string make_flags = " OUTPUT_DIR=" + brisc_out_dir;

            // Build clean
            make_clean_cmd << " BUDA_HOME=" << root << " make -C " << root << "src/firmware/riscv/targets/brisc clean "
                        << make_flags;

            // default ARCH_NAME is grayskull in Makefile
            log_assert(
                (arch_name.compare("grayskull") == 0) || (arch_name.compare("wormhole") == 0) ||
                    (arch_name.compare("wormhole_b0") == 0) || (arch_name.compare("blackhole") == 0),
                "Invalid arch_name");

            make_cmd << " ARCH_NAME=" << arch_name;

            make_cmd << " BUDA_HOME=" << root << " make -C " << root << "src/firmware/riscv/targets/brisc";
            make_cmd << make_flags;
            make_cmd << " PERF_DUMP=" << to_string(is_perf_dump_en);
            make_cmd << " OVERLAY_DECOUPLE=" << to_string(is_overlay_decoupled);
            make_cmd << " INTERMED_DUMP=" << to_string(is_perf_spill_dram);
            make_cmd << " PERF_DUMP_CONCURRENT=" << to_string(is_concurrent_perf);
            make_cmd << " PERF_DUMP_LEVEL=" << to_string(perf_dump_level);

            int enable_tt_llk_dump = 0;
            int enable_tt_log = 0;
            get_tt_log_and_tt_llk_dump(enable_tt_log, enable_tt_llk_dump);

            make_cmd << " ENABLE_TT_LOG=" + to_string(enable_tt_log);
            make_cmd << " ENABLE_TT_LLK_DUMP=" + to_string(enable_tt_llk_dump);

            log_debug(tt::LogCompileTrisc, "    Make clean cmd: {}", make_clean_cmd.str());
            if (!tt::run_command(make_clean_cmd.str(), log_file)) {
                log_fatal(" BRISC clean failed -- cmd: {}", make_clean_cmd.str());
                exit(1);
            }
            log_debug(tt::LogCompileTrisc, "    Make compile cmd: {}", make_cmd.str());
            if (!tt::run_command(make_cmd.str(), log_file)) {
                log_fatal(" BRISC Build failed -- cmd: {}", make_cmd.str());
                exit(1);
            }

            if (std::getenv("BRISC_BIN_CACHE_DIR") != nullptr) {
                try_dump_fw_bin("brisc", std::getenv("BRISC_BIN_CACHE_DIR"), brisc_out_dir);
            }
        }
    }

    // Initialize the first 32-bit value in blob section to 0 before running the first epoch.
    // This signals to nrisc firmware that there is no blob preloaded and the first epoch
    // needs to be loaded from DRAM before running.
    string blob_bin_filename = "blob_init.hex.static";
    fs::path blob_init_bin(root + "/src/blobgen2/" + blob_bin_filename);
    log_assert(fs::exists(blob_init_bin), "Error: {} doesn't exist", blob_init_bin);
    fs::copy(blob_init_bin, out_dir_path + "/blob_init/blob_init.hex");
}

void generate_erisc_fw(const perf::PerfDesc& perf_desc, const std::string& arch_name, const std::string& out_dir_path) {
    string root = buda_home(fs::current_path().string());
    // stringstream make_clean_cmd;
    stringstream make_cmd;

    string erisc_build_dir = fs::absolute(out_dir_path).string() + "/erisc";
    string log_file = fs::absolute(out_dir_path).string() + "/erisc_build.log";
    bool default_epoch_q_num_slots = !parse_env("TT_BACKEND_EPOCH_QUEUE_NUM_SLOTS", false);
    int firmware_num_loop_iterations = parse_env("NUM_EXEC_LOOP_ITERATIONS", 0);
    string tt_backend_erisc_precompiled_binaries_path = "erisc_hex"; //std::string(std::getenv("TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH"));
    string precompiled_erisc_binaries = root + tt_backend_erisc_precompiled_binaries_path;
    bool precompiled_erisc_binaries_flag = tt_backend_erisc_precompiled_binaries_path.size() > 0;
    bool is_perf_dump_en = perf_desc.device_perf_mode != perf::PerfDumpMode::Disable;
    uint32_t perf_dump_level = perf_desc.perf_dump_level;
    bool is_overlay_decoupled = is_perf_dump_en && perf_desc.overlay_decouplings.size() > 0;
    bool kernel_cache_ena = dram_mem::address_map::KERNEL_CACHE_NUM_SLOTS() > 0;
    bool use_default_erisc_fw = default_epoch_q_num_slots && firmware_num_loop_iterations == 0 && !kernel_cache_ena;
    force_create_build_dir(erisc_build_dir);
    if (use_default_erisc_fw) {
        
        fs::path erisc_build_path;
        if(precompiled_erisc_binaries_flag)
        {
            log_debug(tt::LogCompileTrisc, "Using precompiled ERISC binaries");
            erisc_build_path = precompiled_erisc_binaries;
        }
        else
        {
            log_debug(tt::LogCompileTrisc, "Using Default ERISC Bin");
            erisc_build_path = root + "build/src/firmware/riscv/targets/erisc_app/out";
        }
        fs::path erisc_fw_path(erisc_build_path.string() + "/erisc_app.hex");
        log_assert(fs::exists(erisc_fw_path), "Error {} doesn't exist", erisc_fw_path);
        fs::copy(erisc_build_path, erisc_build_dir);
        fs::last_write_time(erisc_build_dir + "/split_iram_l1", fs::file_time_type::clock::now());
    } else {
        log_debug(tt::LogCompileTrisc, "Recompiling ERISC firmware");
        make_cmd << " ARCH_NAME=" << arch_name << " BUDA_HOME=" << root << " make -C " << root
                 << "src/firmware/riscv/targets/erisc";
        make_cmd << " OUTPUT_DIR=" << erisc_build_dir;

        int enable_tt_llk_dump = 0;
        int enable_tt_log = 0;
        get_tt_log_and_tt_llk_dump(enable_tt_log, enable_tt_llk_dump);

        make_cmd << " ENABLE_TT_LOG=" + to_string(enable_tt_log);
        make_cmd << " ENABLE_TT_LLK_DUMP=" + to_string(enable_tt_llk_dump);

        if (!default_epoch_q_num_slots) {
            // Rederive parameters based on the epoch queue slots size
            make_cmd << " EPOCH_QUEUE_NUM_SLOTS=" << std::to_string(epoch_queue::get_epoch_q_num_slots());
            make_cmd << " EPOCH_ENTRY_SIZE_BYTES=" << std::to_string(epoch_queue::get_epoch_table_entry_size_bytes());
            make_cmd << " EPOCH_TABLE_DRAM_ADDRESS=" << std::to_string(epoch_queue::get_epoch_table_dram_addr());
            make_cmd << " EPOCH_ALLOC_QUEUE_SYNC_ADDRESS="
                     << std::to_string(epoch_queue::get_epoch_alloc_queue_sync_addr());
        }

        if (firmware_num_loop_iterations) {
            // For FW looping on device
            make_cmd << " NUM_EXEC_LOOP_ITERATIONS=" << firmware_num_loop_iterations;
        }

        if (kernel_cache_ena) {
            make_cmd << " KERNEL_CACHE_ENA=" << kernel_cache_ena;
        }

        // Configurable base addresses into epoch binary in DRAM for NCRISC FW which depend on whether Kernel Cache is enabled.
        make_cmd << " DRAM_EPOCH_RUNTIME_CONFIG_BASE=" + to_string(dram_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE());
        make_cmd << " DRAM_OVERLAY_BLOB_BASE=" + to_string(dram_mem::address_map::OVERLAY_BLOB_BASE());

        if (is_perf_dump_en) {
            make_cmd << " PERF_DUMP=" << to_string(is_perf_dump_en);
            make_cmd << " OVERLAY_DECOUPLE=" << to_string(is_overlay_decoupled);
            make_cmd << " PERF_DUMP_LEVEL=" << to_string(perf_dump_level);
        }

        log_debug(tt::LogCompileTrisc, "    Make compile cmd: {}", make_cmd.str());
        if (!tt::run_command(make_cmd.str(), log_file)) {
            log_fatal(" ERISC Build failed -- cmd: {}", make_cmd.str());
            exit(1);
        }
    }
}

void generate_ncrisc_fw(
    const perf::PerfDesc& perf_desc, const std::string& arch_name, const std::string& out_dir_path) {
    PROFILE_SCOPE_MS();
    stringstream make_clean_cmd;
    stringstream make_cmd;
    string root = buda_home(fs::current_path().string());

    bool is_perf_dump_en = perf_desc.device_perf_mode != perf::PerfDumpMode::Disable;
    bool is_perf_spill_dram = perf_desc.device_perf_mode == perf::PerfDumpMode::IntermediateDump;
    bool is_concurrent_perf = perf_desc.device_perf_mode == perf::PerfDumpMode::Concurrent;
    bool is_distributed_tables = !parse_env("TT_BACKEND_NON_DISTRIBUTED_EPOCH_TABLE", false);
    bool default_epoch_q_num_slots = !parse_env("TT_BACKEND_EPOCH_QUEUE_NUM_SLOTS", false);
    bool is_emulator_compile = parse_env("TT_BACKEND_EMULATOR_RUN", false);
    uint32_t perf_dump_level = perf_desc.perf_dump_level;
    bool is_dram_decouple_en = perf_desc.dram_decouple_config.size() > 0;
    bool is_overlay_decoupled = is_perf_dump_en && perf_desc.overlay_decouplings.size() > 0;
    int firmware_num_loop_iterations =
        std::getenv("NUM_EXEC_LOOP_ITERATIONS") ? std::stoi(std::getenv("NUM_EXEC_LOOP_ITERATIONS")) : 0;
    bool kernel_cache_ena = dram_mem::address_map::KERNEL_CACHE_NUM_SLOTS() > 0;
    bool is_default_ncrisc_bin = !is_perf_dump_en && !is_perf_spill_dram && perf_dump_level == 0 &&
                                 is_distributed_tables && !is_dram_decouple_en && default_epoch_q_num_slots &&
                                 firmware_num_loop_iterations == 0 && !is_overlay_decoupled && !kernel_cache_ena && !is_emulator_compile;

    fs::path ncrisc_build_path(root + "build/src/firmware/riscv/targets/ncrisc/out");
    string ncrisc_out_dir = fs::absolute(out_dir_path).string() + "/ncrisc";
    string log_file = fs::absolute(out_dir_path).string() + "/ncrisc_build.log";

    if (is_default_ncrisc_bin && fs::exists(ncrisc_build_path)) {
        log_debug(tt::LogCompileTrisc, "Using Default NCRISC Bin");
        force_create_build_dir(ncrisc_out_dir);

        fs::path ncrisc_fw_path(ncrisc_build_path.string() + "/ncrisc.hex");
        if (!fs::exists(ncrisc_fw_path)) {
            log_fatal("Error: {} does not exist", ncrisc_fw_path);
        }
        fs::copy(ncrisc_build_path, ncrisc_out_dir);
    } else {
        bool loaded_ncrisc_bin = false;
        // If set, the variable stores the directory of the compiled ncrisc binaries for a specific configuration;
        // It is up to the user to make sure the ncrisc configuration used for compilation is the right one.
        // This is useful in perf_sweep.py where we run large number of tests with the same perf configuration.
        if (std::getenv("NCRISC_BIN_CACHE_DIR") != nullptr) {
            loaded_ncrisc_bin = try_load_fw_bin("ncrisc", std::getenv("NCRISC_BIN_CACHE_DIR"), root, ncrisc_out_dir);
        }

        if (loaded_ncrisc_bin) {
            return;
        }

        log_debug(tt::LogCompileTrisc, "Recompiling NCRISC firmware");
        std::string make_flags = " OUTPUT_DIR=" + ncrisc_out_dir;

        // Build clean
        make_clean_cmd << " BUDA_HOME=" << root << " make -C " << root << "src/firmware/riscv/targets/ncrisc clean "
                       << make_flags;

        // default ARCH_NAME is grayskull in Makefile
        log_assert(
            (arch_name.compare("grayskull") == 0) || (arch_name.compare("wormhole") == 0) ||
            (arch_name.compare("wormhole_b0") == 0) || (arch_name.compare("blackhole")==0), "Invalid arch name");

        // Build
        if (arch_name.compare("grayskull") != 0) {
            make_cmd << " ARCH_NAME=" << arch_name;
        }

        make_cmd << " BUDA_HOME=" << root << " make -C " << root << "src/firmware/riscv/targets/ncrisc";
        make_cmd << make_flags;
        make_cmd << " PERF_DUMP=" << to_string(is_perf_dump_en);
        make_cmd << " OVERLAY_DECOUPLE=" << to_string(is_overlay_decoupled);
        make_cmd << " INTERMED_DUMP=" << to_string(is_perf_spill_dram);
        make_cmd << " PERF_DUMP_CONCURRENT=" << to_string(is_concurrent_perf);
        make_cmd << " PERF_DUMP_LEVEL=" << to_string(perf_dump_level);
        make_cmd << " DRAM_DECOUPLE=" << to_string(is_dram_decouple_en);

        int enable_tt_llk_dump = 0;
        int enable_tt_log = 0;
        get_tt_log_and_tt_llk_dump(enable_tt_log, enable_tt_llk_dump);

        make_cmd << " ENABLE_TT_LOG=" + to_string(enable_tt_log);
        make_cmd << " ENABLE_TT_LLK_DUMP=" + to_string(enable_tt_llk_dump);

        if (!default_epoch_q_num_slots) {
            // Epoch Command Queue Size was updated during runtime. Rederive values depending on it, since these are
            // used in NCRISC FW.
            make_cmd << " EPOCH_QUEUE_NUM_SLOTS=" << to_string(epoch_queue::get_epoch_q_num_slots());
            make_cmd << " EPOCH_ENTRY_SIZE_BYTES=" << to_string(epoch_queue::get_epoch_table_entry_size_bytes());
            make_cmd << " EPOCH_TABLE_DRAM_ADDRESS=" << to_string(epoch_queue::get_epoch_table_dram_addr());
            make_cmd << " EPOCH_ALLOC_QUEUE_SYNC_ADDRESS=" << to_string(epoch_queue::get_epoch_alloc_queue_sync_addr());
        }

        if (is_distributed_tables == false) {
            make_cmd << " NO_DISTRIBUTED_EPOCH_TABLES=1";
        }
        if (is_emulator_compile) {
            make_cmd << " USE_EMULATOR_DRAM_LAYOUT=1";
        }
        if (firmware_num_loop_iterations > 0) {
            make_cmd << " NUM_EXEC_LOOP_ITERATIONS=" << firmware_num_loop_iterations;
        }
        if (kernel_cache_ena) {
            make_cmd << " KERNEL_CACHE_ENA=" << kernel_cache_ena;
        }

        // Configurable base addresses into epoch binary in DRAM for NCRISC FW which depend on whether Kernel Cache is enabled.
        make_cmd << " DRAM_EPOCH_RUNTIME_CONFIG_BASE=" + to_string(dram_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE());
        make_cmd << " DRAM_OVERLAY_BLOB_BASE=" + to_string(dram_mem::address_map::OVERLAY_BLOB_BASE());

        log_debug(tt::LogCompileTrisc, "    Make clean cmd: {}", make_clean_cmd.str());
        if (!tt::run_command(make_clean_cmd.str(), log_file)) {
            log_fatal(" NCRISC clean failed -- cmd: {}", make_clean_cmd.str());
            exit(1);
        }
        log_debug(tt::LogCompileTrisc, "    Make compile cmd: {}", make_cmd.str());
        if (!tt::run_command(make_cmd.str(), log_file)) {
            log_fatal(" NCRISC Build failed -- cmd: {}", make_cmd.str());
            exit(1);
        }

        if (std::getenv("NCRISC_BIN_CACHE_DIR") != nullptr) {
            try_dump_fw_bin("ncrisc", std::getenv("NCRISC_BIN_CACHE_DIR"), ncrisc_out_dir);
        }
    }
}

UniqueCompileConfigMap get_unique_compilation_configs(
    const string& build_dir_path,
    const map<string, tt_digraph>& graphs,
    const string& device_name,
    const perf::PerfDesc& perf_desc,
    const netlist_workload_data& workload,
    bool compile_for_perf_only) {
    UniqueCompileConfigMap compilation_map;
    int num_total_ops = 0;

    std::string root = buda_home(fs::current_path().string());

    for (auto&& [graph_name, graph] : graphs) {
        int op_index = 0;
        for (const std::pair<const string, tt_op_info>& op_it : graph.my_graph_info.op_map) {
            if (netlist_utils::is_non_tensix_op(op_it.second.type)) {
                log_trace(tt::LogCompileTrisc, "Skipping non-tensix op: {}", op_it.first);
                op_index++;
                continue;
            }
            if (compile_for_perf_only) {
                if (perf_desc.skip_compile_for_op(op_it.first)) {
                    log_warning(
                        tt::LogCompileTrisc,
                        "Skipping op compile for {} since compile_for_perf_only is set and this op does not have any "
                        "decouplings",
                        op_it.first);
                    op_index++;
                    continue;
                }
            }
            std::shared_ptr<tt_op> op = op_it.second.my_op;

            string hlk_file_name = op->cores[0][0]->local_desc.get_hlk_cpp_file_name();
            
            string op_type = op->type;
            bool is_datacopy = netlist_utils::get_unary_op(op->type) == UnaryOp::Datacopy;
            if (is_datacopy) {
                // datacopy can be under names: datacopy, nop, ethernet_datacopy.
                // for hashing purposes, it is important that we use the same name.
                op_type = "datacopy";
            }

            bool is_fused_op = false;
            if (hlk_file_name.find("fused_op") != std::string::npos) {
                hlk_file_name = build_dir_path + "/" + hlk_file_name;
                is_fused_op = true;
            } else {
                hlk_file_name = root + hlk_file_name;
            }

            bool is_perf_dump_en = perf_desc.enabled();
            const unordered_set<perf::PerfTriscDecoupleMode>& perf_trisc_decouplings =
                get_trisc_decouplings_for_op(op_it.second.name, perf_desc);

            string graph_path = get_graph_path(build_dir_path, graph_name);
            string op_path = get_op_path(graph_path, op_index);
            uint32_t relu_config = op->relu_en ? (static_cast<uint32_t>(op->relu_en) |
                                                  static_cast<uint32_t>(op->relu_mode) | (op->relu_threshold << 16))
                                               : 0;

            std::array<uint32_t, 3> kernel_delays = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
            if (perf_desc.op_name_to_kernel_delay.find(op_it.second.name) != perf_desc.op_name_to_kernel_delay.end()) {
                kernel_delays = perf_desc.op_name_to_kernel_delay.at(op_it.second.name);
            }

            std::array<std::pair<int, bool>, 16> kernel_broadcasts = {};
            for (int i = 0; i < op->kernel_broadcasts.size(); i++) {
                kernel_broadcasts[i] = op->kernel_broadcasts[i];
            }

            CompilationConfig op_compilation_config = {
                .op_index = op_index,
                .perf_desc = perf_desc,
                .perf_trisc_decouplings = perf_trisc_decouplings,
                .is_perf_dump_en = is_perf_dump_en,
                .untilize_output = op->untilize_output,
                .pack_microblocks = true,
                .fp32_dest_acc_en = op->fp32_dest_acc_en,
                .relu_config = relu_config,
                .sfpu_execution_thread = op->sfpu_execution_thread,
                .is_fused_op = is_fused_op,
                .graph_name = graph_name,  // Not used for unique
                .loop_count = graph.my_graph_info.input_count,
                .hlk_file_name = hlk_file_name,
                .op_type = op_type,
                .op_path = op_path,  // Not used for unique
                .device_name = device_name,
                .op = op,
                .hlk_desc = &(op->cores[0][0]->local_desc),
                .op_info = &(op_it.second),
                .kernel_delays = kernel_delays,
                .op_input_tile_dims = op->input_tile_dims,
                .op_output_tile_dims = op->output_tile_dims,
                .kernel_broadcasts = kernel_broadcasts,
                .stoch_rnd_mode = op->stoch_rnd_mode
            };

            if (compilation_map.find(op_compilation_config) != compilation_map.end()) {
                compilation_map[op_compilation_config].push_back(op_compilation_config);
            } else {
                compilation_map[op_compilation_config] = {};
            }

            op_index++;
        }
        num_total_ops += op_index;
    }


    log_debug(tt::LogCompileTrisc, "Number of unique HLK/Hex compiles: {}", compilation_map.size());
    log_debug(tt::LogCompileTrisc, "Number of total HLK/Hex compiles: {}", num_total_ops);

    return compilation_map;
}

void write_trisc_unique_ops_yaml(const string& build_dir_path, UniqueCompileConfigMap& compilation_map) {

    PROFILE_SCOPE_MS();

    YAML::Node trisc_unique_ops_map;

    log_trace(tt::LogCompileTrisc, "Unique Compilation Instances:");
    uint32_t count = 0;
    for (auto& it : compilation_map) {
        log_trace(tt::LogCompileTrisc, "[{}]: {} -> ", count, it.first);
        trisc_unique_ops_map[it.first.op_path][it.first.op_path]["graph_name"]  = it.first.graph_name;
        for (auto& e : it.second) {
            log_trace(tt::LogCompileTrisc, "{}, ", e);
            trisc_unique_ops_map[it.first.op_path][e.op_path]["graph_name"] = e.graph_name;
        }
        log_trace(tt::LogCompileTrisc, "\n");
        count++;
    }


    std::ofstream fout(build_dir_path + "/trisc_unique_ops.yaml");
    fout << trisc_unique_ops_map;
    fout.close();

}

void generate_compile_time_constants_file(
    const tt_op* op,
    const string& op_path,
    const string& device_name,
    bool is_fp32_dest_acc_en,
    bool is_perf_dump_en,
    bool is_untilize_output_en,
    bool is_pack_microblocks_en,
    bool is_pack_l1_acc_en,
    bool is_unpack_math_decoupled_en,
    bool is_math_pack_decoupled_en,
    bool is_int_fpu_en,
    uint32_t relu_config,
    uint8_t sfpu_execution_thread,
    std::array<uint32_t, 3> kernel_delays,
    StochRndMode stoch_rnd_mode,
    TileDimReconfigMode is_tile_dim_reconfig_mode,
    bool is_tilize_input_en) {

    const string compile_time_constants_file_path = op_path + "/hlk_compile_time_constants.h";
    ofstream compile_time_constants_file;
    compile_time_constants_file.open(compile_time_constants_file_path);

    compile_time_constants_file << "#include \"hlk_api.h\"" << "\n";
    compile_time_constants_file << "#include \"llk_defs.h\"" << "\n\n";

    // Write the needed constants into the header file
    compile_time_constants_file << "constexpr bool IS_FP32_DEST_ACC_EN = " << is_fp32_dest_acc_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_PERF_DUMP_EN = " << is_perf_dump_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_UNTILIZE_OUTPUT_EN = " << is_untilize_output_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_PACK_MICROBLOCKS_EN = " << is_pack_microblocks_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_PACK_L1_ACC_EN = " << is_pack_l1_acc_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_UNPACK_MATH_DECOUPLED_EN = " << is_unpack_math_decoupled_en
                                << ";\n";
    compile_time_constants_file << "constexpr bool IS_MATH_PACK_DECOUPLED_EN = " << is_math_pack_decoupled_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_INT_FPU_EN = " << is_int_fpu_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_TILE_DIM_UNPACK_RECONFIG_EN = " << is_tile_dim_reconfig_mode.unpack_reconfig_en << ";\n";
    compile_time_constants_file << "constexpr bool IS_TILE_DIM_PACK_RECONFIG_EN = " << is_tile_dim_reconfig_mode.pack_reconfig_en << ";\n";

    if (is_tilize_input_en == true) {
        log_assert(
            device_name.find("blackhole") != string::npos,
            "Tilize input is available on blackhole only, trying to use it on the following arch: {}",
            device_name);
    }

    compile_time_constants_file << "constexpr bool IS_TILIZE_INPUT_EN = " << is_tilize_input_en << ";\n";

    // Relu mathematics
    bool relu_en = (relu_config & 0xf) > 0;
    uint32_t relu_mode = (relu_config & 0xf);
    compile_time_constants_file << "constexpr ReluType RELU_TYPE = ReluType::";
    if (relu_en) {
        if (relu_mode == 3) {
            compile_time_constants_file << "MAX_THRESHOLD_RELU;\n";
        } else {
            compile_time_constants_file << "MIN_THRESHOLD_RELU;\n";
        }
    } else {
        compile_time_constants_file << "NO_RELU;\n";
    }
    compile_time_constants_file << "constexpr uint32_t RELU_THRESHOLD = " << (int)(relu_en ? relu_config >> 16 : 0)
                                << ";\n";

    // Sfpu execution thread
    compile_time_constants_file << "constexpr SfpuExecutionThread SFPU_EXECUTION_THREAD = SfpuExecutionThread::";
    SfpuExecutionThread sfpu_execution_thread_enum = (SfpuExecutionThread)sfpu_execution_thread;
    switch (sfpu_execution_thread_enum) {
        case SfpuExecutionThread::Math: compile_time_constants_file << "Math;\n"; break;
        case SfpuExecutionThread::Pack: compile_time_constants_file << "Pack;\n"; break;
        default:
            log_fatal(
                "Invalid sfpu execution thread detected in CompileTrisc, the passed in value is: {}",
                sfpu_execution_thread);
            break;
    }

    // Stochastic rounding modes
    compile_time_constants_file << "constexpr StochRndType STOCH_RND_TYPE = StochRndType::";
    switch (stoch_rnd_mode) {
        case StochRndMode::None: compile_time_constants_file << "None;\n"; break;
        case StochRndMode::Fpu: compile_time_constants_file << "Fpu;\n"; break;
        case StochRndMode::Pack: compile_time_constants_file << "Pack;\n"; break;
        case StochRndMode::All: compile_time_constants_file << "All;\n"; break;
        default:
            log_fatal("Invalid stochastic round mode detected in CompileTrisc, the passed in value is: {}", stoch_rnd_mode);
            break;
    }

    // Kernel delays
    compile_time_constants_file << "constexpr bool IS_KERNEL_UNPACK_DELAY_EN = " << (bool)(kernel_delays[0] != 0)
                                << ";\n";
    compile_time_constants_file << "constexpr bool IS_KERNEL_MATH_DELAY_EN = " << (bool)(kernel_delays[1] != 0)
                                << ";\n";
    compile_time_constants_file << "constexpr bool IS_KERNEL_PACK_DELAY_EN = " << (bool)(kernel_delays[2] != 0)
                                << ";\n";

    // DstTileFaceLayout for kernel setup and matmul
    // It is always RowMajor, except on grayskull matmul in non-fused op, where it is ColMajor
    bool is_grayskull = device_name.find("grayskull") != string::npos;
    compile_time_constants_file
        << "constexpr DstTileFaceLayout MATMUL_AND_KERNEL_SETUP_DST_FACE_LAYOUT = DstTileFaceLayout::";
    if (is_grayskull && (netlist_utils::is_valid_matmul_op(op->type) || netlist_utils::is_valid_depthwise_op(op->type))) {
        compile_time_constants_file << "ColMajor;\n";
    } else {
        compile_time_constants_file << "RowMajor;\n";
    }

    compile_time_constants_file << "constexpr DstSync DST_SYNC = DstSync::";
    if (op->get_dst_size() == DstSize::HalfSize) {
        compile_time_constants_file << "SyncHalf;\n";
    } else if (op->get_dst_size() == DstSize::FullSize) {
        compile_time_constants_file << "SyncFull;\n";
    } else {
        log_assert(false, "Invalid dest size: {} for op {}", (int)op->get_dst_size(), op->name);
    }

    if (netlist_utils::is_valid_sfpu_op(op->type)) {
        SfpuOp sfpu_op = netlist_utils::get_sfpu_op(op->type);
        compile_time_constants_file << "\n";
        compile_time_constants_file << "constexpr SfpuOp SFPU_KERNEL_TYPE = SfpuOp::" << netlist_utils::sfpu_op_to_string(sfpu_op) << ";\n";
        compile_time_constants_file << "// We define SFPU_KERNEL_TYPE_DEFINED so we don't use SFPU_KERNEL_TYPE from eltwise_unary_sfpu.h\n";
        compile_time_constants_file << "#define SFPU_KERNEL_TYPE_DEFINED\n";
    }

    if (netlist_utils::is_valid_binary_op(op->type)) {
        BinaryOp binary_op = netlist_utils::get_binary_op(op->type);
        compile_time_constants_file << "\n";
        compile_time_constants_file << "constexpr BinaryOp BINARY_KERNEL_TYPE = BinaryOp::" << netlist_utils::binary_op_to_string(binary_op) << ";\n";
        compile_time_constants_file << "// We define BINARY_KERNEL_TYPE_DEFINED so we don't use BINARY_KERNEL_TYPE from eltwise_binary.h\n";
        compile_time_constants_file << "#define BINARY_KERNEL_TYPE_DEFINED\n";
    }

    compile_time_constants_file.close();
}

void compile_for_op(
    const string& graph_name,
    int op_index,
    const tt_op_info& op_info,
    const perf::PerfDesc& perf_desc,
    string build_dir_path,
    const string& device_name,
    int input_count,
    const netlist_workload_data& workload) {

    if (netlist_utils::is_non_tensix_op(op_info.type)) {
        return;
    }

    string root = buda_home(fs::current_path().string());

    // get op
    std::shared_ptr<tt_op> shared_op = op_info.my_op;
    tt_op* op = shared_op.get();
    log_trace(tt::LogCompileTrisc, "Compiling Op {}", op_info);
    log_trace(tt::LogCompileTrisc, "   index: {} Grid shape: {}", op_index, op->get_grid_shape());

    // currently assuming HLK is the same across all cores
    string hlk_cpp_file_name = op->cores[0][0]->local_desc.get_hlk_cpp_file_name();
    if (hlk_cpp_file_name.find("fused_op") != std::string::npos) {
        hlk_cpp_file_name = build_dir_path + "/" + hlk_cpp_file_name;
    } else {
        hlk_cpp_file_name = root + hlk_cpp_file_name;
    }

    bool is_perf_dump_en = perf_desc.enabled();

    const std::pair<bool, bool>& decouplings = tt::check_perf_trisc_decouple_exist(op->name, perf_desc);
    bool unp_math_decouple = decouplings.first;
    bool math_pack_decouple = decouplings.second;

    string graph_path = get_graph_path(build_dir_path, graph_name);
    string op_path = get_op_path(graph_path, op_index);

    fs::create_directory(op_path);

    uint32_t relu_config =
        op->relu_en
            ? (static_cast<uint32_t>(op->relu_en) | static_cast<uint32_t>(op->relu_mode) | (op->relu_threshold << 16))
            : 0;
    std::array<uint32_t, 3> delays = {0, 0, 0};
    if (perf_desc.op_name_to_kernel_delay.find(op_info.name) != perf_desc.op_name_to_kernel_delay.end()) {
        delays = perf_desc.op_name_to_kernel_delay.at(op_info.name);
    }

    generate_struct_init_file(*op, op_path, op->cores[0][0]->local_desc.hlk_args_descriptor);
    generate_constexpr_hlk_args(*op, op_path);

    generate_data_format_descriptors(op, op_path, device_name);
    generate_tile_dim_descriptors(op, op_path);
    generate_math_fidelity_descriptor(op, op_path);
    generate_math_approx_mode_descriptor(op, op_path);
    generate_loop_count_file(op, input_count, op_path);
    generate_perf_events_target_inputs(op, input_count, op_path, perf_desc);
    generate_perf_resource_decouple_config(op_info, op_path, perf_desc, workload, graph_name);
    generate_kernel_slowdown_config(op_info, op_path, perf_desc);

    // Copy src hlk to op folder
    string hlk_copy_path = op_path + "/hlk.cpp";
    fs::copy_file(hlk_cpp_file_name, hlk_copy_path);

    generate_compile_time_constants_file(
        op,
        op_path,
        device_name,
        op->fp32_dest_acc_en,
        is_perf_dump_en,
        op->untilize_output,
        true,
        op->pack_l1_acc_en,
        unp_math_decouple,
        math_pack_decouple,
        op->int_fpu_en,
        relu_config,
        static_cast<std::uint8_t>(op->sfpu_execution_thread),
        delays,
        op->stoch_rnd_mode,
        op->is_tile_dim_reconfig_mode,
        op->tilize_input);

    if (!netlist_utils::is_non_tensix_op(op->type)) {
        compile_ckernels_for_all_triscs(device_name, root, op_path, perf_desc, graph_name);
    }
}

void print_non_tensix_op_placement(const tt_op* op_info, vector<vector<string>>& all_cores_print) {
    if (netlist_utils::is_valid_ethernet_op(op_info->type)) {
        // Do nothing since we only print tensix cores for now and multiple ethernet data copies can be mapped to a
        // single core
    } else {
        log_assert(false, "Unsupported non tensix op type: {}", op_info->type);
    }
}

void print_placement(const netlist_workload_data& workload, const string& build_dir_path) {
    log_assert(fs::exists(build_dir_path), "build dir does not exist");
    log_assert(build_dir_path != "", "build dir path is not set");
    string output_file_name = build_dir_path + "/placement_report.yaml";
    YAML::Node output_yaml;
    log_debug(tt::LogCompileTrisc, "Writing the placement information in {}", output_file_name);
    std::ofstream output_file(output_file_name);
    const int cell_width = 4;
    int worker_grid_size_x = -1;
    int worker_grid_size_y = -1;
    if (workload.device_info.arch == tt::ARCH::GRAYSKULL) {
        worker_grid_size_x = 12;
        worker_grid_size_y = 10;
    } else if (workload.device_info.arch == tt::ARCH::WORMHOLE || workload.device_info.arch == tt::ARCH::WORMHOLE_B0) {
        worker_grid_size_x = 8;
        worker_grid_size_y = 10;
    } else if (workload.device_info.arch == tt::ARCH::BLACKHOLE) {
        worker_grid_size_x = 14;
        worker_grid_size_y = 10;
    } else {
        log_warning(tt::LogCompileTrisc, "Skipping placement print, since device type is not supported");
        return;
    }

    for (const auto& graph_it : workload.graphs) {
        YAML::Node graph_yaml;
        vector<vector<string>> all_cores_print(worker_grid_size_y, vector<string>(worker_grid_size_x, "."));
        unordered_map<string, int> op_type_to_num_cores;

        int op_index = 0;
        for (const auto& op_it : graph_it.second.my_graph_info.op_map) {
            string op_name = op_it.first;
            const std::shared_ptr<tt_op> op = op_it.second.my_op;

            int grid_shape_y = op->grid_shape.at(0);
            int grid_shape_x = op->grid_shape.at(1);

            string op_type = op_it.second.type;
            if (op_type_to_num_cores.find(op_type) != op_type_to_num_cores.end()) {
                op_type_to_num_cores.at(op_type) += grid_shape_x * grid_shape_y;
            } else {
                op_type_to_num_cores.insert({op_type, grid_shape_x * grid_shape_y});
            }

            if (netlist_utils::is_non_tensix_op(op->type)) {
                print_non_tensix_op_placement(op.get(), all_cores_print);
            } else {
                for (int y_grid = 0; y_grid < grid_shape_y; y_grid++) {
                    for (int x_grid = 0; x_grid < grid_shape_x; x_grid++) {
                        log_assert(y_grid < op->cores.size(), "y_grid out of range in cores lists for op");
                        log_assert(x_grid < op->cores.at(y_grid).size(), "x_grid out of range in cores lists for op");
                        int core_y_id = op->cores.at(y_grid).at(x_grid)->get_logical_absolute_row_id();
                        int core_x_id = op->cores.at(y_grid).at(x_grid)->get_logical_absolute_col_id();
                        log_assert(core_y_id < all_cores_print.size(), "core_y_id out of range");
                        log_assert(core_x_id < all_cores_print.at(core_y_id).size(), "core_x_id");
                        all_cores_print.at(core_y_id).at(core_x_id) = to_string(op_index);
                    }
                }
            }
            op_index++;
        }
        vector<string> grid_rows;
        for (int y = 0; y < all_cores_print.size(); y++) {
            std::stringstream ss;
            for (int x = 0; x < all_cores_print.at(y).size(); x++) {
                ss << std::left << std::setw(cell_width) << all_cores_print.at(y).at(x);
            }
            grid_rows.push_back(ss.str());
        }

        graph_yaml["placement"] = grid_rows;
        YAML::Node op_type_yaml;
        int total_used_cores = 0;
        for (const auto& op_type_it : op_type_to_num_cores) {
            float core_utilization = op_type_it.second * 100.0 / (worker_grid_size_x * worker_grid_size_y);
            op_type_yaml[op_type_it.first]["Num_cores"] = op_type_it.second;
            op_type_yaml[op_type_it.first]["Core_utilization(%)"] = core_utilization;
            total_used_cores += op_type_it.second;
        }
        graph_yaml["Op_types"] = op_type_yaml;
        float total_core_utilization = total_used_cores * 100.0 / (worker_grid_size_x * worker_grid_size_y);
        graph_yaml["Total_core_utilization(%)"] = total_core_utilization;
        output_yaml[graph_it.first] = graph_yaml;
    }
    output_file << output_yaml;
    output_file.close();
}

void generate_trisc_fw(
    const netlist_workload_data& workload,
    string device_name,
    perf::PerfDesc& perf_desc,
    bool compile_for_perf_only,
    string build_dir_path,
    int num_threads) {
    PROFILE_SCOPE_MS();

    remove_graph_build_dirs(build_dir_path, num_threads);

    log_debug(tt::LogCompileTrisc, "Compiling TRISC kernels");

    print_placement(workload, build_dir_path);

    // Create graph output dirs
    tt::parallel_for(
        workload.graphs.begin(),
        workload.graphs.end(),
        [&](auto& graph_it) { fs::create_directories(get_graph_path(build_dir_path, graph_it.first)); },
        num_threads);

    string root = buda_home(fs::current_path().string());
    if (!workload.parser.fused_ops_op_map.empty()) {
        std::string hlks_build_dir_path = build_dir_path + "/hlks";
        fs::create_directory(hlks_build_dir_path);

        Net2Hlks net2hlks(workload.parser, hlks_build_dir_path);
        net2hlks.output_hlks();
    }

    UniqueCompileConfigMap unique_compile_configs = get_unique_compilation_configs(
        build_dir_path, workload.graphs, device_name, perf_desc, workload, compile_for_perf_only);


    // To support Kernel Cache
    write_trisc_unique_ops_yaml(build_dir_path, unique_compile_configs);

    tt::parallel_for(
        unique_compile_configs.begin(),
        unique_compile_configs.end(),
        [&](auto& unique_compile_config) {
            compile_for_op(
                unique_compile_config.first.graph_name,
                unique_compile_config.first.op_index,
                *unique_compile_config.first.op_info,
                unique_compile_config.first.perf_desc,
                build_dir_path,
                device_name,
                unique_compile_config.first.loop_count,
                workload);
        },
        num_threads);

    // Create symbolic links for redundant compile configs
    tt::parallel_for(
        unique_compile_configs.begin(),
        unique_compile_configs.end(),
        [&](const auto& unique_compile_config) {
            for (const auto& redundant_compile_config : unique_compile_config.second) {
                std::string relative_path = std::filesystem::relative(
                    std::filesystem::path(unique_compile_config.first.op_path),
                    std::filesystem::path(redundant_compile_config.op_path).parent_path());
                fs::create_directory_symlink(relative_path, redundant_compile_config.op_path);
            }
        },
        num_threads);

    for (const std::pair<const string, tt_digraph>& graph_it : workload.graphs) {
        string graph_path = get_graph_path(build_dir_path, graph_it.first);
        generate_op_info_file(graph_it.second.my_graph_info.op_map, graph_path);
    }

    log_debug(tt::LogCompileTrisc, "Compiling TRISC kernels, Done!");
}

std::string get_ckernels_thread_filename(int thread_id) {
    static string used_kernels[3] = {"chlkc_unpack", "chlkc_math", "chlkc_pack"};
    log_assert(thread_id >= 0 && thread_id < 3, "Invalid thread id {}", thread_id);
    return used_kernels[thread_id];
}

void compile_ckernels_for_trisc(
    const std::string& chlkc_src_dir, int thread_id, const std::string& compile_cmd, const std::string& link_cmd) {
    log_trace(tt::LogCompileTrisc, "Make compile cmd: {}", compile_cmd);
    std::string log_file_name =
        fs::absolute(chlkc_src_dir).string() + "/" + "hlk_ckernels_compile_thread" + to_string(thread_id) + ".log";
    std::string err_file_name =
        fs::absolute(chlkc_src_dir).string() + "/" + "hlk_ckernels_compile_thread" + to_string(thread_id) + ".err.log";
    tt::cmd_result_t result = tt::run_command(compile_cmd, log_file_name, err_file_name);
    if (!result.success) {
        string err_msg = "Build ckernels/src failed for a thread " + to_string(thread_id) + " with CKernels '" +
                         get_ckernels_thread_filename(thread_id) + "'\n" + result.message;
        err_msg += "\nLog: " + log_file_name + "\nError: " + err_file_name;
        throw std::runtime_error(err_msg);
    }

    log_trace(tt::LogCompileTrisc, "Make link cmd: {}", link_cmd);
    result = tt::run_command(link_cmd, log_file_name, err_file_name);
    if (!result.success) {
        string err_msg = "Link ckernels/src failed for a thread " + to_string(thread_id) + " with CKernels '" +
                         get_ckernels_thread_filename(thread_id) + "'\n" + result.message;
        err_msg += "\nLog: " + log_file_name + "\nError: " + err_file_name;
        throw std::runtime_error(err_msg);
    }
}

int get_trisc_mailbox_address(int thread_id) {
    log_assert(thread_id >= 0 && thread_id < 3, "Invalid thread id {}", thread_id);
    uint32_t TRISC_BASE = l1_mem::address_map::TRISC_BASE;
    uint32_t TRISC_L1_MAILBOX_OFFSET = l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET;
    uint32_t trisc_sizes[3] = {
        l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE};

    uint32_t trisc_mailbox_addresses[3] = {
        TRISC_BASE + TRISC_L1_MAILBOX_OFFSET,
        TRISC_BASE + trisc_sizes[0] + TRISC_L1_MAILBOX_OFFSET,
        TRISC_BASE + trisc_sizes[0] + trisc_sizes[1] + TRISC_L1_MAILBOX_OFFSET};
    return trisc_mailbox_addresses[thread_id];
}

std::string get_trisc_output_dir(const std::string& chlkc_src_dir, int thread_id) {
    stringstream ckernels_compile_output_dir;
    ckernels_compile_output_dir << chlkc_src_dir << "/tensix_thread" << (uint)thread_id;
    return fs::absolute(ckernels_compile_output_dir.str()).string();
}

std::string get_trisc_compile_cmd(
    const std::string& root,
    const std::string& device_name,
    const perf::PerfDesc& perf_desc,
    const std::string& chlkc_src_dir,
    int thread_id) {
    bool is_perf_dump_en = perf_desc.enabled();
    bool is_perf_spill_dram = is_perf_dump_en && perf_desc.device_perf_mode == perf::PerfDumpMode::IntermediateDump;
    bool is_concurrent = is_perf_dump_en && perf_desc.device_perf_mode == perf::PerfDumpMode::Concurrent;
    bool is_overlay_decouple_en = is_perf_dump_en && perf_desc.overlay_decouplings.size() > 0;

    const string make_ckernels_compile_dir = root + "/src/ckernels/" + device_name + "/buda/common";

    std::stringstream make_src_cmd;
    make_src_cmd << "make -C " << make_ckernels_compile_dir;

    make_src_cmd << " -j8 ";
    make_src_cmd << " KERNELS='" << get_ckernels_thread_filename(thread_id) << '\'';
    make_src_cmd << " PERF_DUMP=" << to_string(is_perf_dump_en);
    make_src_cmd << " INTERMED_DUMP=" << to_string(is_perf_spill_dram);
    make_src_cmd << " PERF_DUMP_CONCURRENT=" << to_string(is_concurrent);
    make_src_cmd << " PERF_DUMP_LEVEL=" << to_string(perf_desc.perf_dump_level);
    make_src_cmd << " MAILBOX_ADDR=" << get_trisc_mailbox_address(thread_id);
    make_src_cmd << " OVERLAY_DECOUPLE=" << std::to_string(is_overlay_decouple_en);
    make_src_cmd << " HLK_INC=" << fs::absolute(chlkc_src_dir).string();
    make_src_cmd << " HLK_LIB_INC=" << fs::absolute(root + "/hlks/inc/").string();
    make_src_cmd << " OUTPUT_DIR=" << get_trisc_output_dir(chlkc_src_dir, thread_id);
    make_src_cmd << " BUDA_HOME=" << root;
    make_src_cmd << " FIRMWARE_NAME=tensix_thread" << (uint32_t)thread_id;

    int enable_tt_llk_dump = 0;
    int enable_tt_log = 0;
    get_tt_log_and_tt_llk_dump(enable_tt_log, enable_tt_llk_dump);

    make_src_cmd << " ENABLE_TT_LOG=" + to_string(enable_tt_log);
    make_src_cmd << " ENABLE_TT_LLK_DUMP=" + to_string(enable_tt_llk_dump);
    return make_src_cmd.str();
}

std::string get_trisc_link_cmd(
    const std::string& root, const std::string& device_name, const std::string& chlkc_src_dir, int thread_id) {
    uint32_t trisc_sizes[3] = {
        l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE};
    const string make_ckernels_link_dir = root + "/src/ckernels";
    uint32_t TRISC_BASE = l1_mem::address_map::TRISC_BASE;
    uint32_t TRISC_LOCAL_MEM_RESERVED = l1_mem::address_map::TRISC_LOCAL_MEM_SIZE;

    std::stringstream make_cmd;
    make_cmd << "make -C " << make_ckernels_link_dir;
    make_cmd << " TRISC0_SIZE=" + to_string(trisc_sizes[0]) + " TRISC1_SIZE=" + to_string(trisc_sizes[1]) +
                    " TRISC2_SIZE=" + to_string(trisc_sizes[2]);
    make_cmd << " ARCH_NAME=" + device_name;
    make_cmd << " TRISC_BASE=" + to_string(TRISC_BASE);
    make_cmd << " TRISC_LOCAL_MEM_RESERVED=" + to_string(TRISC_LOCAL_MEM_RESERVED);
    make_cmd << " FIRMWARE_NAME=tensix_thread" << (uint32_t)thread_id;
    make_cmd << " KERNELS=''";
    make_cmd << " LINKER_SCRIPT_NAME=trisc" << (uint32_t)thread_id << ".ld";
    make_cmd << " TEST='chlkc'";
    make_cmd << " OUTPUT_DIR=" << get_trisc_output_dir(chlkc_src_dir, thread_id);
    make_cmd << " CKERNELS_COMMON_OUT_DIR=" << get_trisc_output_dir(chlkc_src_dir, thread_id);
    make_cmd << " CLEAN_OUTPUT_DIR=0";
    make_cmd << " BUDA_HOME=" << root;

    int enable_tt_llk_dump = 0;
    int enable_tt_log = 0;
    get_tt_log_and_tt_llk_dump(enable_tt_log, enable_tt_llk_dump);

    make_cmd << " ENABLE_TT_LOG=" + to_string(enable_tt_log);
    make_cmd << " ENABLE_TT_LLK_DUMP=" + to_string(enable_tt_llk_dump);
    
    return make_cmd.str();
}

void compile_ckernels_for_all_triscs(
    string device_name,
    string root,
    string chlkc_src_dir,
    const perf::PerfDesc& perf_desc,
    const string& graph_name) {
    fs::remove("hlk_ckernels_compile.log");  // clean the log file

    tt::parallel_for(
        0, 3,
        [&](int thread_id) {
            compile_ckernels_for_trisc(
                chlkc_src_dir,
                thread_id,
                get_trisc_compile_cmd(root, device_name, perf_desc, chlkc_src_dir, thread_id),
                get_trisc_link_cmd(root, device_name, chlkc_src_dir, thread_id));
        }, 3);
}

void print_tile_dims(stringstream& out, const vector<vector<int>>& tile_dims) {
    for (int i = 0; i < tile_dims.size(); i++) {
        out << "{" << tile_dims[i][0] << ", " << tile_dims[i][1] << "}, ";
    }
}

void print_tile_num_faces(stringstream& out, const vector<vector<int>>& tile_dims) {
    for (int i = 0; i < tile_dims.size(); i++) {
        if ((tile_dims[i][0] <= 16) && (tile_dims[i][1] <= 16)) {
            out << "1, ";
        } else if ((tile_dims[i][0] == 32) && (tile_dims[i][1] == 32)) {
            out << "4, ";
        } else {
            out << "2, ";
        }
    }
}

void print_tile_face_r_dim(stringstream& out, const vector<vector<int>>& tile_dims) {
    for (int i = 0; i < tile_dims.size(); i++) {
        if (tile_dims[i][0] < 16) {
            out << tile_dims[i][0] << ", ";
        } else {
            out << "16, ";
        }
    }
}

void print_partial_face(stringstream& out, const vector<vector<int>>& tile_dims) {
    for (int i = 0; i < tile_dims.size(); i++) {
        if (tile_dims[i][0] < 16) {
            out << "1, ";
        } else {
            out << "0, ";
        }
    }
}

void print_narrow_tile(stringstream& out, const vector<vector<int>>& tile_dims) {
    for (int i = 0; i < tile_dims.size(); i++) {
        if (tile_dims[i][1] <= 16) {
            out << "1, ";
        } else {
            out << "0, ";
        }
    }
}

void print_tile_sizes(stringstream& out, const vector<int>& tile_sizes) {
    for (int i = 0; i < tile_sizes.size(); i++) {
        out << tile_sizes[i] << ", ";
    }
}

void generate_unpack_math_tile_dims_file(
    const string& file_name,
    const string& tensix_thread,
    vector<vector<int>>& input_tile_dims,
    vector<vector<int>>& param_tile_dims,
    vector<vector<int>>& intermed_tile_dims,
    const vector<int>& input_tile_sizes,
    const vector<int>& param_tile_sizes,
    const vector<int>& intermed_tile_sizes) {
    int total_num_elements = NUM_OPERANDS + NUM_MAX_PARAM_BUFFERS_PER_CORE + NUM_MAX_INTERMED_BUFFERS_PER_CORE;

    log_assert(
        input_tile_dims.size() + param_tile_dims.size() + intermed_tile_dims.size() == total_num_elements,
        "There must be a total of {} inputs, params and intermed buffers in an op!",
        total_num_elements);

    // tile_dims
    stringstream tile_dims_str;
    {
        tile_dims_str << "constexpr std::uint32_t unpack_tile_dims[" << total_num_elements
                      << "][2] = {" << endl;
        print_tile_dims(tile_dims_str, input_tile_dims);
        tile_dims_str << "// Input Buffers" << endl;

        print_tile_dims(tile_dims_str, param_tile_dims);
        tile_dims_str << "// Param Buffers" << endl;

        print_tile_dims(tile_dims_str, intermed_tile_dims);
        tile_dims_str << "// Intermed Buffers" << endl;

        tile_dims_str << "};" << endl;
    }

    // tile_num_faces
    stringstream tile_num_faces_str;
    {
        tile_num_faces_str << "constexpr std::uint32_t unpack_tile_num_faces[" << total_num_elements
                           << "] = {" << endl;
        print_tile_num_faces(tile_num_faces_str, input_tile_dims);
        tile_num_faces_str << "// Input Buffers" << endl;

        print_tile_num_faces(tile_num_faces_str, param_tile_dims);
        tile_num_faces_str << "// Param Buffers" << endl;

        print_tile_num_faces(tile_num_faces_str, intermed_tile_dims);
        tile_num_faces_str << "// Intermed Buffers" << endl;

        tile_num_faces_str << "};" << endl;
    }

    // tile_face_r_dim
    stringstream tile_face_r_dim_str;
    {
        tile_face_r_dim_str << "constexpr std::uint32_t unpack_tile_face_r_dim[" << total_num_elements
                            << "] = {" << endl;
        print_tile_face_r_dim(tile_face_r_dim_str, input_tile_dims);
        tile_face_r_dim_str << "// Input Buffers" << endl;

        print_tile_face_r_dim(tile_face_r_dim_str, param_tile_dims);
        tile_face_r_dim_str << "// Param Buffers" << endl;

        print_tile_face_r_dim(tile_face_r_dim_str, intermed_tile_dims);
        tile_face_r_dim_str << "// Intermed Buffers" << endl;

        tile_face_r_dim_str << "};" << endl;
    }

    // tile_with_partial_face
    stringstream tile_with_partial_face_str;
    {
        tile_with_partial_face_str << "constexpr bool unpack_partial_face[" << total_num_elements
                                   << "] = {" << endl;
        print_partial_face(tile_with_partial_face_str, input_tile_dims);
        tile_with_partial_face_str << "// Input Buffers" << endl;

        print_partial_face(tile_with_partial_face_str, param_tile_dims);
        tile_with_partial_face_str << "// Param Buffers" << endl;

        print_partial_face(tile_with_partial_face_str, intermed_tile_dims);
        tile_with_partial_face_str << "// Intermed Buffers" << endl;

        tile_with_partial_face_str << "};" << endl;
    }

    stringstream tile_is_narrow;
    {
        tile_is_narrow << "constexpr bool unpack_narrow_tile[" << total_num_elements << "] = {"
                       << endl;
        print_narrow_tile(tile_is_narrow, input_tile_dims);
        tile_is_narrow << "// Input Buffers" << endl;

        print_narrow_tile(tile_is_narrow, param_tile_dims);
        tile_is_narrow << "// Param Buffers" << endl;

        print_narrow_tile(tile_is_narrow, intermed_tile_dims);
        tile_is_narrow << "// Intermed Buffers" << endl;

        tile_is_narrow << "};" << endl;
    }

    // tile_sizes
    stringstream tile_sizes_str;
    {
        tile_sizes_str << "constexpr std::uint32_t unpack_tile_sizes[" << total_num_elements << "] = {"
                       << endl;

        print_tile_sizes(tile_sizes_str, input_tile_sizes);
        tile_sizes_str << "// Input Buffers" << endl;

        print_tile_sizes(tile_sizes_str, param_tile_sizes);
        tile_sizes_str << "// Param Buffers" << endl;

        print_tile_sizes(tile_sizes_str, intermed_tile_sizes);
        tile_sizes_str << "// Intermed Buffers" << endl;

        tile_sizes_str << "};" << endl;
    }

    ofstream file_stream;
    file_stream.open(file_name);

    file_stream << tile_dims_str.str() << endl;
    file_stream << tile_num_faces_str.str() << endl;
    file_stream << tile_face_r_dim_str.str() << endl;
    file_stream << tile_with_partial_face_str.str() << endl;
    file_stream << tile_is_narrow.str() << endl;
    file_stream << tile_sizes_str.str() << endl;

    file_stream.close();
}

void generate_pack_tile_dims_file(
    const string& file_name,
    const vector<vector<int>>& output_tile_dims,
    const vector<vector<int>>& intermed_tile_dims,
    const vector<int>& output_tile_sizes,
    const vector<int>& intermed_tile_sizes) {
    int total_num_elements = NUM_MAX_OUT_BUFFERS_PER_CORE + NUM_MAX_INTERMED_BUFFERS_PER_CORE;

    log_assert(
        output_tile_dims.size() + intermed_tile_dims.size() == total_num_elements,
        "There must be a total of {} output and intermed buffers in an op!",
        total_num_elements);

    // tile_dims
    stringstream tile_dims_str;
    {
        tile_dims_str << "constexpr std::uint32_t pack_tile_dims[" << total_num_elements << "][2] = {" << endl;

        print_tile_dims(tile_dims_str, output_tile_dims);
        tile_dims_str << "// Output Buffers" << endl;

        print_tile_dims(tile_dims_str, intermed_tile_dims);
        tile_dims_str << "// Intermed Buffers" << endl;

        tile_dims_str << "};" << endl;
    }

    // tile_num_faces
    stringstream tile_num_faces_str;
    {
        tile_num_faces_str << "constexpr std::uint32_t pack_tile_num_faces[" << total_num_elements << "] = {" << endl;
        print_tile_num_faces(tile_num_faces_str, output_tile_dims);
        tile_num_faces_str << "// Output Buffers" << endl;

        print_tile_num_faces(tile_num_faces_str, intermed_tile_dims);
        tile_num_faces_str << "// Intermed Buffers" << endl;

        tile_num_faces_str << "};" << endl;
    }

    // tile_face_r_dim
    stringstream tile_face_r_dim_str;
    {
        tile_face_r_dim_str << "constexpr std::uint32_t pack_tile_face_r_dim[" << total_num_elements << "] = {" << endl;
        print_tile_face_r_dim(tile_face_r_dim_str, output_tile_dims);
        tile_face_r_dim_str << "// Output Buffers" << endl;

        print_tile_face_r_dim(tile_face_r_dim_str, intermed_tile_dims);
        tile_face_r_dim_str << "// Intermed Buffers" << endl;

        tile_face_r_dim_str << "};" << endl;
    }

    // tile_with_partial_face
    stringstream tile_with_partial_face_str;
    {
        tile_with_partial_face_str << "constexpr bool pack_partial_face[" << total_num_elements << "] = {" << endl;
        print_partial_face(tile_with_partial_face_str, output_tile_dims);
        tile_with_partial_face_str << "// Output Buffers" << endl;

        print_partial_face(tile_with_partial_face_str, intermed_tile_dims);
        tile_with_partial_face_str << "// Intermed Buffers" << endl;

        tile_with_partial_face_str << "};" << endl;
    }

    stringstream tile_is_narrow;
    {
        tile_is_narrow << "constexpr bool pack_narrow_tile[" << total_num_elements << "] = {" << endl;
        print_narrow_tile(tile_is_narrow, output_tile_dims);

        tile_is_narrow << "// Output Buffers" << endl;

        print_narrow_tile(tile_is_narrow, intermed_tile_dims);
        tile_is_narrow << "// Intermed Buffers" << endl;

        tile_is_narrow << "};" << endl;
    }

    // tile_sizes
    stringstream tile_sizes_str;
    {
        tile_sizes_str << "constexpr std::uint32_t pack_tile_sizes[" << total_num_elements << "] = {" << endl;

        print_tile_sizes(tile_sizes_str, output_tile_sizes);
        tile_sizes_str << "// Output Buffers" << endl;

        print_tile_sizes(tile_sizes_str, intermed_tile_sizes);
        tile_sizes_str << "// Intermed Buffers" << endl;

        tile_sizes_str << "};" << endl;
    }

    ofstream file_stream;
    file_stream.open(file_name);

    file_stream << tile_dims_str.str() << endl;
    file_stream << tile_num_faces_str.str() << endl;
    file_stream << tile_face_r_dim_str.str() << endl;
    file_stream << tile_with_partial_face_str.str() << endl;
    file_stream << tile_is_narrow.str() << endl;
    file_stream << tile_sizes_str.str() << endl;

    file_stream.close();
}

// Generates tile_dim arrays for each operand
void generate_tile_dim_descriptors(const tt_op* op, const string& out_dir_path) {
    tt_hlk_desc& desc = op->cores[0][0]->local_desc;
    int invalid_tile_dim = (int)32;
    int invalid_tile_size_bytes = invalid_tile_dim;  // same value (0xff) as invalid tile_dim

    // produce input tile dims and tile sizes
    vector<vector<int>> input_tile_dims;
    vector<int> input_tile_sizes;
    {
        for (int i = 0; i < op->input_tile_dims.size(); i++) {
            input_tile_dims.push_back({(int)op->input_tile_dims[i][0], (int)op->input_tile_dims[i][1]});
        }

        // set other input tile dims to invalid
        for (int i = op->input_tile_dims.size(); i < NUM_MAX_IN_BUFFERS_PER_CORE; i++) {
            input_tile_dims.push_back({invalid_tile_dim, invalid_tile_dim});
        }

        // set input tile dims
        for (int i = 0; i < op->input_tile_dims.size(); i++) {
            bool has_header = op->type == "embedding" ? (i != 0) : true;  // check this

            int input_tile_size_bytes = desc.input_buf_dataformat_arr[i] != DataFormat::Invalid
                                            ? size::get_tile_size_in_bytes(
                                                  desc.input_buf_dataformat_arr[i],
                                                  has_header,
                                                  op->input_tile_dims[i][0],
                                                  op->input_tile_dims[i][1])
                                            : invalid_tile_size_bytes;
            input_tile_sizes.push_back(input_tile_size_bytes);
        }

        // set others to invalid
        for (int i = op->input_tile_dims.size(); i < NUM_MAX_IN_BUFFERS_PER_CORE; i++) {
            input_tile_sizes.push_back(invalid_tile_size_bytes);
        }
    }

    // produce param tile dims and tile sizes
    vector<vector<int>> param_tile_dims;
    vector<int> param_tile_sizes;
    {
        // If input tile dims is > max_num_inputs, we spill the rest in param_tile_dims (happens in fused ops)
        if (input_tile_dims.size() > NUM_MAX_IN_BUFFERS_PER_CORE) {
            for (int i = NUM_MAX_IN_BUFFERS_PER_CORE; i < input_tile_dims.size(); i++) {
                param_tile_dims.push_back(input_tile_dims[i]);
                param_tile_sizes.push_back(input_tile_sizes[i]);
            }

            // remove extra elements from input_tile_dims array
            int original_size = input_tile_dims.size();
            for (int i = NUM_MAX_IN_BUFFERS_PER_CORE; i < original_size; i++) {
                input_tile_dims.pop_back();
                input_tile_sizes.pop_back();
            }
        }

        // set param tile dims to invalid
        for (int i = param_tile_dims.size(); i < NUM_MAX_PARAM_BUFFERS_PER_CORE; i++) {
            param_tile_dims.push_back({invalid_tile_dim, invalid_tile_dim});
        }

        // set param tile sizes to invalid
        for (int i = param_tile_sizes.size(); i < NUM_MAX_PARAM_BUFFERS_PER_CORE; i++) {
            param_tile_sizes.push_back(invalid_tile_size_bytes);
        }
    }

    // produce output tile dims and tile sizes
    vector<vector<int>> output_tile_dims;
    vector<int> output_tile_sizes;
    {
        // set out0 tile dim
        output_tile_dims.push_back({(int)op->output_tile_dims[0], (int)op->output_tile_dims[1]});

        // set other output tile dims to invalid
        for (int i = 1; i < NUM_MAX_OUT_BUFFERS_PER_CORE; i++) {
            output_tile_dims.push_back({invalid_tile_dim, invalid_tile_dim});
        }

        // set out0 tile size
        bool out_has_header = !op->untilize_output;
        int out_0_tile_size_in_bytes = desc.output_buf_dataformat_arr[0] != DataFormat::Invalid
                                           ? size::get_tile_size_in_bytes(
                                                 desc.output_buf_dataformat_arr[0],
                                                 out_has_header,
                                                 op->output_tile_dims[0],
                                                 op->output_tile_dims[1])
                                           : invalid_tile_size_bytes;
        output_tile_sizes.push_back(out_0_tile_size_in_bytes);

        // set other outputs to invalid
        for (int i = 1; i < NUM_MAX_OUT_BUFFERS_PER_CORE; i++) {
            output_tile_sizes.push_back(invalid_tile_size_bytes);
        }
    }

    // produce intermed tile dims and tile sizes
    vector<vector<int>> intermed_tile_dims;
    vector<int> intermed_tile_sizes;
    {
        // set intermed tile dims to out tile dim
        for (int i = 0; i < NUM_MAX_INTERMED_BUFFERS_PER_CORE; i++) {
            intermed_tile_dims.push_back({(int)op->output_tile_dims[0], (int)op->output_tile_dims[1]});
        }

        // set intermed tile sizes
        for (int i = 0; i < NUM_MAX_INTERMED_BUFFERS_PER_CORE; i++) {
            int intermed_tile_size_in_bytes = desc.intermediate_buf_dataformat_arr[i] != DataFormat::Invalid
                                                  ? size::get_tile_size_in_bytes(
                                                        desc.intermediate_buf_dataformat_arr[i],
                                                        true,
                                                        op->output_tile_dims[0],
                                                        op->output_tile_dims[1])
                                                  : invalid_tile_size_bytes;
            intermed_tile_sizes.push_back(intermed_tile_size_in_bytes);
        }
    }

    generate_unpack_math_tile_dims_file(
        out_dir_path + "/" + "chlkc_unpack_tile_dims.h",
        "unpack",
        input_tile_dims,
        param_tile_dims,
        intermed_tile_dims,
        input_tile_sizes,
        param_tile_sizes,
        intermed_tile_sizes);

    generate_unpack_math_tile_dims_file(
        out_dir_path + "/" + "chlkc_math_tile_dims.h",
        "math",
        input_tile_dims,
        param_tile_dims,
        intermed_tile_dims,
        input_tile_sizes,
        param_tile_sizes,
        intermed_tile_sizes);

    generate_pack_tile_dims_file(
        out_dir_path + "/" + "chlkc_pack_tile_dims.h",
        output_tile_dims,
        intermed_tile_dims,
        output_tile_sizes,
        intermed_tile_sizes);
}

void generate_data_format_descriptors(tt_op* op, const string& out_dir_path, const string& arch_name) {
    string out_file_name_base = "chlkc_";
    string out_file_name_suffix = "_data_format.h";
    string unpack_data_format_descs = out_dir_path + "/" + out_file_name_base + "unpack" + out_file_name_suffix;
    string math_data_format_descs = out_dir_path + "/" + out_file_name_base + "math" + out_file_name_suffix;
    string pack_data_format_descs = out_dir_path + "/" + out_file_name_base + "pack" + out_file_name_suffix;

    // assuming all cores within a op have the same desc
    tt_hlk_desc& desc = op->cores[0][0]->local_desc;

    // Determine what the packformat should be
    tt::get_pack_data_format(desc.output_buf_dataformat_arr, desc.intermediate_buf_dataformat_arr);

    // Determine unpack conditional format based on input array (packer can convert mixed exponent widths for output
    // format)
    ExpPrecision unpack_exp_prec =
        tt::get_input_data_exp_precision(desc.input_buf_dataformat_arr, desc.intermediate_buf_dataformat_arr);
    DataFormat unpack_conditional_dst_format =
        (unpack_exp_prec == ExpPrecision::A) ? DataFormat::Float16 : DataFormat::Float16_b;

    if (tt::is_all_fp32_formats(desc.input_buf_dataformat_arr) &&
        tt::is_all_fp32_formats(desc.intermediate_buf_dataformat_arr)) {
        if (tt::is_all_fp32_formats(desc.output_buf_dataformat_arr)) {
            // TODO: When math_df is added, default exp width format will not be assumed, but will be taken from math df
            // tenstorrent/budabackend#925
            unpack_conditional_dst_format = DataFormat::Float16;
        } else {
            ExpPrecision output_unpack_exp_prec = tt::get_data_exp_precision(desc.output_buf_dataformat_arr);
            unpack_conditional_dst_format =
                (output_unpack_exp_prec == ExpPrecision::A) ? DataFormat::Float16 : DataFormat::Float16_b;
        }
    }

    tt::check_valid_in_out_data_formats(
        desc.input_buf_dataformat_arr,
        desc.output_buf_dataformat_arr,
        desc.param_buf_dataformat_arr,
        desc.intermediate_buf_dataformat_arr);

    //////////////
    // UNPACK
    /////////////
    // assuming unpack_src_format and unpack_dst_format are equal
    string unpack_src_format = "";
    string unpack_dst_format = "";
    vector<DataFormat> src_formats;
    vector<DataFormat> dst_formats;
    src_formats = tt::get_unpack_src_formats(
        desc.input_buf_dataformat_arr, desc.param_buf_dataformat_arr, desc.intermediate_buf_dataformat_arr);
    dst_formats = tt::get_unpack_dst_formats(
        desc.input_buf_dataformat_arr,
        desc.param_buf_dataformat_arr,
        desc.intermediate_buf_dataformat_arr,
        desc.output_buf_dataformat_arr,
        unpack_conditional_dst_format,
        op->fp32_dest_acc_en, 
        op->int_fpu_en);
    log_assert(
        src_formats.size() == 24 && dst_formats.size() == 24,
        "There must be 8 unpack src/dst formats for each input, param, and intermediate operands.");
    for (int i = 0; i < 24; i++) {
        unpack_src_format += to_string((int)src_formats[i]) + ",";
        unpack_dst_format += to_string((int)dst_formats[i]) + ",";
    }
    ofstream file_stream;

    file_stream.open(unpack_data_format_descs);
    file_stream << "constexpr std::uint32_t unpack_src_format[24] = {" << endl;
    file_stream << "    ";
    file_stream << unpack_src_format;
    file_stream << endl << "};" << endl;
    file_stream << "constexpr std::uint32_t unpack_dst_format[24] = {" << endl;
    file_stream << "    ";
    file_stream << unpack_dst_format;
    file_stream << endl << "};" << endl;
    file_stream.close();

    //////////////
    // MATH
    /////////////
    // generate math format for data format reconfigs

    file_stream.open(math_data_format_descs);
    file_stream << "constexpr std::uint32_t unpack_src_format[24] = {" << endl;
    file_stream << "    ";
    file_stream << unpack_src_format;
    file_stream << endl << "};" << endl;
    file_stream << "constexpr std::uint32_t unpack_dst_format[24] = {" << endl;
    file_stream << "    ";
    file_stream << unpack_dst_format;
    file_stream << endl << "};" << endl;
    file_stream.close();

    //////////////
    // PACK
    /////////////
    string pack_src_format = "";
    string pack_dst_format = "";

    src_formats = tt::get_pack_src_formats(
        desc.input_buf_dataformat_arr,
        desc.param_buf_dataformat_arr,
        desc.intermediate_buf_dataformat_arr,
        desc.output_buf_dataformat_arr,
        unpack_conditional_dst_format,
        op->fp32_dest_acc_en,
        op->int_fpu_en,
        tt::get_arch_from_string(arch_name));
    dst_formats = tt::get_pack_dst_formats(
        desc.input_buf_dataformat_arr,
        desc.param_buf_dataformat_arr,
        desc.intermediate_buf_dataformat_arr,
        desc.output_buf_dataformat_arr);
    log_assert(
        src_formats.size() == 16 && dst_formats.size() == 16,
        "There must be 8 pack src/dst formats for each output, and intermediate operands.");
    for (int i = 0; i < 16; i++) {
        pack_src_format += to_string((int)src_formats[i]) + ",";
        pack_dst_format += to_string((int)dst_formats[i]) + ",";
    }

    file_stream.open(pack_data_format_descs);
    file_stream << "constexpr std::uint32_t pack_src_format[16] = {" << endl;
    file_stream << "    ";
    file_stream << pack_src_format;
    file_stream << endl << "};" << endl;
    file_stream << "constexpr std::uint32_t pack_dst_format[16] = {" << endl;
    file_stream << "    ";
    file_stream << pack_dst_format;
    file_stream << endl << "};" << endl;
    file_stream.close();
}

void generate_math_fidelity_descriptor(tt_op* op, string out_dir_path) {
    string math_fidelity_descriptor = out_dir_path + "/" + "chlkc_math_fidelity.h";

    // assuming all cores within a op have the same desc
    tt_hlk_desc& desc = op->cores[0][0]->local_desc;

    ofstream file_stream;

    file_stream.open(math_fidelity_descriptor);
    file_stream << "constexpr std::uint32_t MATH_FIDELITY_DESC = " << (int)desc.get_hlk_math_fidelity() << ";" << endl;
    file_stream.close();
}

void generate_math_approx_mode_descriptor(tt_op* op, string out_dir_path) {
    string approx_descriptor = out_dir_path + "/" + "chlkc_math_approx_mode.h";

    // assuming all cores within a op have the same desc
    tt_hlk_desc& desc = op->cores[0][0]->local_desc;

    ofstream file_stream;

    file_stream.open(approx_descriptor);
    file_stream << "constexpr bool APPROX = " << std::boolalpha << desc.get_hlk_math_approx_mode() << ";" << endl;
    file_stream.close();
}

void generate_loop_count_file(tt_op* op, int input_count, const string& out_dir_path) {
    string loop_count_file = out_dir_path + "/" + "loop_count.h";

    ofstream file_stream;

    file_stream.open(loop_count_file);
    file_stream << "constexpr std::uint32_t arg_loop_count = " << input_count << ";" << endl;
    file_stream.close();
}

void generate_op_info_file(const std::map<string, tt_op_info>& op_map, const string& out_dir_path) {
    string op_info_file = out_dir_path + "/" + "op_info.txt";

    ofstream file_stream;

    file_stream.open(op_info_file);
    int op_index = 0;
    for (const std::pair<const string, tt_op_info>& op_it : op_map) {
        std::shared_ptr<tt_op> op = op_it.second.my_op;
        string op_name = op->name;
        file_stream << "op_" << op_index << ": " << op_name << endl;
        op_index++;
    }
    file_stream.close();
}

void generate_perf_events_target_inputs(
    tt_op* op, int input_count, const string& out_dir_path, const perf::PerfDesc& perf_desc) {
    string target_inputs_file = out_dir_path + "/" + "perf_events_target_inputs.h";
    ofstream file_stream(target_inputs_file);
    if (!perf_desc.enabled()) {
        return;
    }

    bool override_target_inputs = perf_desc.target_inputs.size() > 0;
    vector<int> in_target_inputs = perf::decode_target_inputs_str(perf_desc.target_inputs, input_count);
    std::vector<int> target_inputs;

    if (perf_desc.device_perf_mode != perf::PerfDumpMode::Disable) {
        if (override_target_inputs) {
            // Filter invalid target input values
            for (auto target_input : in_target_inputs) {
                if (target_input >= 0) {
                    if (target_input < input_count) {
                        bool target_input_already_exist =
                            std::find(target_inputs.begin(), target_inputs.end(), target_input) != target_inputs.end();
                        if (!target_input_already_exist) {
                            target_inputs.push_back(target_input);
                        } else {
                            log_warning(
                                tt::LogRuntime,
                                "Performance target input index {} will be skipped since it already exists in list of "
                                "inputs ({})",
                                target_input,
                                fmt::join(target_inputs, ", "));
                        }
                    } else {
                        log_warning(
                            tt::LogRuntime,
                            "Performance target input index {} will be skipped since it is not in range 0 to "
                            "input_count-1 ({})",
                            target_input,
                            input_count - 1);
                    }
                } else {
                    int target_input_from_last = input_count + target_input;
                    bool target_input_already_exist =
                        std::find(target_inputs.begin(), target_inputs.end(), target_input_from_last) !=
                        target_inputs.end();
                    if (target_input >= -1 * input_count) {
                        if (!target_input_already_exist) {
                            target_inputs.push_back(target_input_from_last);
                        } else {
                            log_warning(
                                tt::LogRuntime,
                                "Performance target input index {} which translates to index {} will be skipped since "
                                "it already exists in list of inputs ({})",
                                target_input,
                                target_input_from_last,
                                fmt::join(target_inputs, ", "));
                        }
                    } else {
                        log_warning(
                            tt::LogRuntime,
                            "Performance target input index {} will be skipped since it is not in range 0 to "
                            "-(input_count) ({})",
                            target_input,
                            -1 * (input_count));
                    }
                }
            }
        } else {
            target_inputs = {0, input_count - 1};
            if (perf_desc.perf_dump_level > 0) {
                // Always add the middle input to the list if in perf_dump_level > 0
                // First check to see if it already doesn't exist inside the vector
                if (std::find(target_inputs.begin(), target_inputs.end(), (input_count - 1) / 2) ==
                    target_inputs.end()) {
                    target_inputs.push_back((input_count - 1) / 2);
                }
            }
        }
        if (perf_desc.measure_steady_state_perf) {
            int quarter_input_idx = input_count / 4;
            int three_q_input_idx = 3 * quarter_input_idx;
            if (std::find(target_inputs.begin(), target_inputs.end(), quarter_input_idx) == target_inputs.end() &&
                quarter_input_idx < input_count) {
                target_inputs.push_back(quarter_input_idx);
            }
            if (std::find(target_inputs.begin(), target_inputs.end(), three_q_input_idx) == target_inputs.end() &&
                three_q_input_idx < input_count) {
                target_inputs.push_back(three_q_input_idx);
            }
            if (std::find(target_inputs.begin(), target_inputs.end(), 0) == target_inputs.end()) {
                target_inputs.push_back(0);
            }
            if (std::find(target_inputs.begin(), target_inputs.end(), input_count - 1) == target_inputs.end()) {
                target_inputs.push_back(input_count - 1);
            }
        }
    }

    std::sort(target_inputs.begin(), target_inputs.end());
    log_trace(tt::LogRuntime, "Recording performance trace for input indices: {}", fmt::join(target_inputs, ", "));

    std::int32_t max_input_idx = (1 << perf::perf_inputs_number_of_bits) - 1;
    for (int input_idx : target_inputs) {
        log_assert(input_idx <= max_input_idx, "Target input idx is greater than maximum number of inputs performance infra can track on device. Number of bits allocated for input idx on device = {}", perf::perf_inputs_number_of_bits);
    }

    const int num_actual_targets = target_inputs.size();
    // The buffer size available for each thread after double buffering is (l1_mem::address_map::TRISC_PERF_BUF_SIZE)/2.
    // Max number of events we can record in each half of the buffer will be that size divided by 4, since each event
    // will be 4 bytes. This feature will only be used in the SingleDumpPerEpoch mode. We currently assume all trisc
    // threads have the same perf buffer size.
    int num_events_per_input;
    if (num_actual_targets > 0) {
        num_events_per_input = (perf_desc.get_perf_buf_size(0) >> 2) / num_actual_targets;
    } else {
        num_events_per_input = perf_desc.get_perf_buf_size(0) >> 2;
    }
    target_inputs.push_back(-1);  // Always push a -1 at the end of the list, to avoid illegal memory access in ckernel.
    // target input must have a size of at least 3.
    while (target_inputs.size() < 3) {
        target_inputs.push_back(-1);
    }

    file_stream << "const int perf_events_target_inputs[" << to_string(target_inputs.size())
                << "] __attribute__ ((section (\".text.consts\"))) = {\n";
    for (auto& target_input : target_inputs) {
        file_stream << to_string(target_input) << ", ";
    }
    file_stream << "};\n";
    file_stream << "const int num_events_per_input = " << to_string(num_events_per_input) << ";\n";
    file_stream.close();
}

void generate_struct_init_file(const tt_op& op, const string& out_dir_path, const tt_hlk_args_desc& hlk_args_descriptor) {
    string struct_h_file_name = out_dir_path + "/" + "hlk_args_struct_init.h";
    ofstream file_stream;
    file_stream.open(struct_h_file_name);

    file_stream << hlk_args_descriptor.print();
}

void generate_constexpr_hlk_args(const tt_op& op, string out_dir_path) {
    string constexpr_hlk_args = out_dir_path + "/hlk_args_constexpr.h";
    ofstream file_stream;
    file_stream.open(constexpr_hlk_args);

    file_stream << "#pragma once" << std::endl;
    file_stream << "constexpr int kernel_broadcast[";
    file_stream << op.kernel_broadcasts.size() << "] = {";
    for (int i = 0; i < op.kernel_broadcasts.size(); i++) {
        if (i != 0) {
            file_stream << ", ";
        }
        file_stream << op.kernel_broadcasts[i].first;
    }
    file_stream << "};" << std::endl;

    file_stream << "constexpr int kernel_broadcast_per_t[";
    file_stream << op.kernel_broadcasts.size() << "] = {";
    for (int i = 0; i < op.kernel_broadcasts.size(); i++) {
        if (i != 0) {
            file_stream << ", ";
        }
        file_stream << op.kernel_broadcasts[i].second;
    }
    file_stream << "};" << std::endl;
}

void generate_kernel_slowdown_config(const tt_op_info& op_info, string out_dir_path, const perf::PerfDesc& perf_desc) {
    string target_inputs_file = out_dir_path + "/" + "kernel_slowdown_config.h";

    ofstream file_stream(target_inputs_file);

    file_stream << "#pragma once\n\n";
    uint32_t unpack_delay = 0;
    uint32_t math_delay = 0;
    uint32_t pack_delay = 0;
    if (perf_desc.op_name_to_kernel_delay.find(op_info.name) != perf_desc.op_name_to_kernel_delay.end()) {
        std::array<uint32_t, 3> delays = perf_desc.op_name_to_kernel_delay.at(op_info.name);
        unpack_delay = delays.at(0);
        math_delay = delays.at(1);
        pack_delay = delays.at(2);
    }
    log_assert(!(unpack_delay > 0 && math_delay > 0), "Inserting both unpack and math delay is not allowed");
    file_stream << "#define INSERT_UNPACK_DELAY " << std::to_string(unpack_delay) << "\n";
    file_stream << "#define INSERT_MATH_DELAY " << std::to_string(math_delay) << "\n";
    file_stream << "#define INSERT_PACK_DELAY " << std::to_string(pack_delay) << "\n";
    log_trace(
        tt::LogCompileTrisc,
        "Op slowdown config -- OP={}, INSERT_UNPACK_DELAY={}, INSERT_MATH_DELAY={}, INSERT_PACK_DELAY={}",
        op_info.name,
        std::to_string(unpack_delay),
        std::to_string(math_delay),
        std::to_string(pack_delay));
}

void generate_perf_resource_decouple_config(
    const tt_op_info& op_info,
    string out_dir_path,
    const perf::PerfDesc& perf_desc,
    const netlist_workload_data& workload,
    const string& graph_name) {
    string target_inputs_file = out_dir_path + "/" + "perf_res_decouple.h";

    ofstream file_stream(target_inputs_file);

    if (!perf_desc.enabled()) {
        return;
    }

    file_stream << "#pragma once\n\n";

    const std::pair<bool, bool>& trisc_decouple = tt::check_perf_trisc_decouple_exist(op_info.name, perf_desc);

    const bool& decouple_unp_math = trisc_decouple.first;
    const bool& decouple_math_pack = trisc_decouple.second;
    file_stream << "#define SKIP_UNP " << std::to_string(decouple_unp_math) << "\n";
    file_stream << "#define MATH_PACK_DECOUPLE " << std::to_string(decouple_math_pack) << "\n";

    log_trace(
        tt::LogCompileTrisc,
        "Trisc Perf Decouplings -- OP={}, SKIP_UNP={}, MATH_PACK_DECOUPLE={}",
        op_info.name,
        std::to_string(decouple_unp_math),
        std::to_string(decouple_math_pack));

    file_stream.close();
}

bool try_load_fw_bin(
    const string& risc_name, const string& load_bin_dir, const string& root, const string& fw_out_dir) {
    string hex_output = root + load_bin_dir + "/" + risc_name + ".hex";
    if (fs::exists((fs::path(hex_output)))) {
        log_trace(tt::LogCompileTrisc, "Found {} cached bin at {}! Loading...", risc_name, load_bin_dir);
        fs::path load_bin_path(root + load_bin_dir);
        fs::copy(load_bin_path, fw_out_dir);
        return true;
    } else {
        log_trace(
            tt::LogCompileTrisc,
            "Proceeding without the cached {} bin, {} doesn't exist.",
            risc_name,
            hex_output);
        return false;
    }
}

void try_dump_fw_bin(const string& risc_name, const string& dump_bin_dir, const string& fw_out_dir) {
    fs::path dump_bin_dir_path(dump_bin_dir);

    if (fs::exists(dump_bin_dir_path) && (!fs::is_directory(dump_bin_dir_path) || !fs::is_empty(dump_bin_dir_path))) {
        log_trace(tt::LogCompileTrisc, "Skipping {} bin dump, {} is not empty.", risc_name, dump_bin_dir);
    } else {
        log_trace(tt::LogCompileTrisc, "Dumping {} bin to {}...", risc_name, dump_bin_dir);
        fs::create_directories(dump_bin_dir_path);
        fs::copy(fw_out_dir, dump_bin_dir_path);
    }
}

}  // namespace tt
