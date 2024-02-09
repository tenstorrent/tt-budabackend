// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <experimental/filesystem>

#include "tt_backend.hpp"
#include "runtime.hpp"
#include "utils.hpp"
#include "verif.hpp"

namespace fs = std::experimental::filesystem;

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    int opt_level;
    bool run_silicon;
    std::string netlist_path;
    std::string arch_name;
    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::string network_description_file_path;
    std::tie(opt_level, args)    = tt::args::get_command_option_uint32_and_remaining_args(args, "--O", 0);
    std::tie(run_silicon, args)  = tt::args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = tt::args::get_command_option_and_remaining_args(args, "--netlist", "default_netlist.yaml");
    std::tie(network_description_file_path, args) = tt::args::get_command_option_and_remaining_args(args, "--network-desc", "");


    perf::PerfDesc perf_desc(args, netlist_path);
    tt::args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);
    config.cluster_descriptor_path = network_description_file_path;

    netlist_workload_data workload(netlist_path);

    tt_backend_config backend_config = {
        .type = run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = workload.device_info.arch,
        .optimization_level = opt_level, 
        .output_dir=output_dir,
    };

    std::vector<std::string> io_names = {};
    for (auto &io : workload.queues) {
        tt_queue_info &q_info = io.second.my_queue_info;
        if (q_info.input == "HOST") {
            io_names.push_back(q_info.name);
        }
    }

    // ------------------------------------------------------------------------
    // Baseline compile and run mode
    // ------------------------------------------------------------------------
    {
        std::string build = output_dir + "/compile_and_run";
        log_info(tt::LogTest, "Backend MODE = CompileAndRun, build = {}", build);

        backend_config.mode = DEVICE_MODE::CompileAndRun;
        backend_config.output_dir = build;

        std::shared_ptr<tt_backend> backend = tt_backend::create(netlist_path, backend_config);
        log_assert(backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");

        std::shared_ptr<tt_runtime> runtime = std::dynamic_pointer_cast<tt_runtime>(backend);
        vector<tt_dram_io_desc> input_io_desc = runtime->get_host_input_io_desc();
        tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor);

        for (std::string program : workload.program_order) {
            log_assert(backend->run_program(program, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
        }

        log_assert(backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get closed");
    }

    // ------------------------------------------------------------------------
    // Run-only mode picking up a previous compile-and-run build
    // ------------------------------------------------------------------------
    {
        std::string build = output_dir + "/run_only_1";
        log_info(tt::LogTest, "Backend MODE = RunOnly, build = {}", build);
        fs::copy(output_dir + "/compile_and_run", build, fs::copy_options::recursive);

        backend_config.mode = DEVICE_MODE::RunOnly;
        backend_config.output_dir = build;
    
        std::shared_ptr<tt_backend> backend = tt_backend::create(netlist_path, backend_config);
        log_assert(backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");

        std::shared_ptr<tt_runtime> runtime = std::dynamic_pointer_cast<tt_runtime>(backend);
        vector<tt_dram_io_desc> input_io_desc = runtime->get_host_input_io_desc();
        tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor);

        for (std::string program : workload.program_order) {
            log_assert(backend->run_program(program, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
        }

        log_assert(backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get closed");
    }

    // ------------------------------------------------------------------------
    // Compile-only mode to generate build for a later run
    // ------------------------------------------------------------------------
    {
        std::string build = output_dir + "/compile_only";
        log_info(tt::LogTest, "Backend MODE = CompileOnly, build = {}", build);

        backend_config.mode = DEVICE_MODE::CompileOnly;
        backend_config.output_dir = build;
    
        std::shared_ptr<tt_backend> backend = tt_backend::create(netlist_path, backend_config);
        log_assert(backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");
        log_assert(backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get closed");
    }

    // ------------------------------------------------------------------------
    // Run-only mode picking up a previous compile-and-run build
    // ------------------------------------------------------------------------
    {
        std::string build = output_dir + "/run_only_2";
        log_info(tt::LogTest, "Backend MODE = RunOnly, build = {}", build);
        fs::copy(output_dir + "/compile_only", build, fs::copy_options::recursive);

        backend_config.mode = DEVICE_MODE::RunOnly;
        backend_config.output_dir = build;
    
        std::shared_ptr<tt_backend> backend = tt_backend::create(netlist_path, backend_config);
        log_assert(backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");

        std::shared_ptr<tt_runtime> runtime = std::dynamic_pointer_cast<tt_runtime>(backend);
        vector<tt_dram_io_desc> input_io_desc = runtime->get_host_input_io_desc();
        tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor);

        for (std::string program : workload.program_order) {
            log_assert(backend->run_program(program, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");
        }

        log_assert(backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get closed");
    }

    return 0;
}

