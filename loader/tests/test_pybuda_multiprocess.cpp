// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"

using namespace verif::random;

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;

    int seed;
    int num_pushes;
    bool run_silicon;
    std::string netlist_path;
    std::string arch_name;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(seed, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", 1);
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_basic/netlist_unary_multicore.yaml");
    std::tie(arch_name, args)    = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    verif_args::validate_remaining_args(args);

    tt_rnd_set_seed(seed);

    // Backend setup
    tt_backend_config config = {
        .type = run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = arch_name == "grayskull" ? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE,
        .optimization_level = 0, // disable epoch preload since we don't run program in this test
        .output_dir = output_dir};

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);
    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);

    // ----------------------------------------------------------------
    // PyBuda API for initializing backend
    // ----------------------------------------------------------------
    backend.initialize();

    backend.finish();

    int status;
    int num_processes = tt_rnd_int(1, 8);
    pid_t pid = 0;

    // Fork all child processes
    for (int i = 0; i < num_processes; i++) {
        pid = fork();
        if (pid == 0) { // child
            log_info(tt::LogTest, "Intialize child pid={}, parent pid={}", getpid(), getppid());
            log_assert(tt::backend::initialize_child_process(output_dir) == tt::DEVICE_STATUS_CODE::Success, "initialize_child_process failed");
            log_assert(tt::io::info.proc_id == getpid(), "Invalid process ID");
            break;
        }
        else if (pid == -1) {
            log_fatal("Process error!");
            break; // out on failure
        }
    }

    // Random init-to-finish duration
    sleep(tt_rnd_int(0, 5));

    for (int i = 0; i < num_processes; i++) {
        if (pid == 0) { // child
            log_info(tt::LogTest, "Finish child pid={}, parent pid={}", getpid(), getppid());
            log_assert(tt::io::info.proc_id == getpid(), "Invalid process ID");
            log_assert(tt::backend::finish_child_process() == tt::DEVICE_STATUS_CODE::Success, "finish_child_process failed");
            return 0;
        }
    }

    // Join all child processes
    while ((pid = waitpid(-1, &status, 0)) != -1) {
        log_info(tt::LogTest, "Process with pid={} terminated", pid);
    }

    log_assert(pass, "Test failed");
    return 0;
}
