// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <boost/process.hpp>
#include "verif.hpp"
#include "common/param_lib.hpp"

using namespace verif::random;

namespace bp = boost::process;

// Duplicate test commands are not supported (would conflict with device_id and tt_build out dir). Remove white spaces
// which are commonly different between each test cmd with how they are launched by CI.
void check_test_cmds(const std::vector<std::string> &test_cmds){

    log_assert(test_cmds.size() > 0, "Must have at least have 1 command via --test-cmds!");

    std::set<std::string> test_cmds_set;
    for (auto &test : test_cmds){
        std::string test_cmd = test;
        test_cmd.erase(std::remove_if(test_cmd.begin(), test_cmd.end(), ::isspace), test_cmd.end());
        if (test_cmds_set.count(test_cmd) > 0){
            log_fatal("Duplicate test commands are not allowed. Found duplicate: {}", test);
        }
        test_cmds_set.insert(test_cmd);
    }
}

// Something unique for different invocations of this test, used for child process log file names by default.
std::string get_log_prefix(const std::vector<std::string> &args){
    std::string arg_combined;
    for (auto &arg : args){
        arg_combined += arg;
    }
    std::string args_hash = std::to_string(std::hash<std::string>{}(arg_combined));
    return args_hash;
}

// Just in case this test is called from different envs.
std::string get_cmd_path(){
    static std::string path = parse_env<std::string>("BUDA_HOME", "./");
    return path;
}

// Automatically release and reserve a device for a given username
void release_and_reserve_mmio_device(int num_devices, std::string username){

        std::string script_path = "ci/reserve_device.py";

        std::string release_cmd = get_cmd_path() + script_path + " --release-all --user " + username;
        std::string cmd_msg = "Running: " + release_cmd + "\n";
        log_info(tt::LogTest, cmd_msg.c_str());
        int rt = system(release_cmd.c_str());
        log_assert(rt == 0, "Issue releasing boards for username: {}",  username);

        std::string reserve_cmd = get_cmd_path() + script_path + " --reserve " + std::to_string(num_devices) + " --user " + username;
        cmd_msg = "Running: " + reserve_cmd + "\n";
        log_info(tt::LogTest, cmd_msg.c_str());
        rt = system(reserve_cmd.c_str());
        log_assert(rt == 0, "Issue reserving boards for username: {}", username);

}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
    std::string log_prefix_hash = get_log_prefix(args); // Include all args before they are removed.

    bool pass = true;

    std::vector<std::string> test_cmds, test_logs;
    std::string cmdline_arg, log_prefix;
    int same_test_n_processes = 0;
    bool unique_reservation_users_per_test = false;

    // Keep getting as many test commands as provided. Originally were comma separated, but this is more flexible.
    while (1){
        std::tie(cmdline_arg, args) = verif_args::get_command_option_and_remaining_args(args, "--test-cmd", "");
        if (cmdline_arg == "")
            break;
        test_cmds.push_back(cmdline_arg);
    }

    std::tie(log_prefix, args) = verif_args::get_command_option_and_remaining_args(args, "--log-prefix", log_prefix_hash);
    std::tie(same_test_n_processes, args) = verif_args::get_command_option_int32_and_remaining_args(args, "--same-test-n-processes", 0);
    std::tie(unique_reservation_users_per_test, args) =  verif_args::has_command_option_and_remaining_args(args, "--unique-reservation-user-per-test");

    // Issue the same test N number of times, just changing the seed (or any flag)
    if (same_test_n_processes > 0){
        log_assert(test_cmds.size() == 1, "Must only have 1 test command when using this mode");
        std::vector<std::string> test_cmds_new;
        for (int i=0; i<same_test_n_processes; i++){
            test_cmds_new.push_back(test_cmds.at(0) + " " + "--seed " + std::to_string(i)); 
        }
        test_cmds = test_cmds_new;
    }

    // This feature assumes same single netlist run multiple times in parallel targeting different devices via reservation script.
    log_assert(!unique_reservation_users_per_test or same_test_n_processes>0, "unique_reservation_users_per_test=1 only works with same_test_n_processes>0");

    verif_args::validate_remaining_args(args);

    check_test_cmds(test_cmds);

    auto env = boost::this_process::environment();
    // std::string default_arch = "grayskull";
    // if (env.find("ARCH_NAME") == env.end()) {
    //     log_warning(tt::LogTest, "Missing ARCH_NAME, default to {}", default_arch);
    //     env["ARCH_NAME"] = default_arch;
    // }

    std::unordered_map<std::string, std::shared_ptr<bp::child>> running_process_map;
    std::unordered_map<std::string, std::shared_ptr<bp::child>> completed_process_map;


    std::string log_dir = std::experimental::filesystem::path(__FILE__).stem().string() + "_logs";
    std::string cmd = "mkdir -p " + log_dir;
    auto rt = system(cmd.c_str());

    // Set up child process log file names for writing to and pointing user to.
    for (int test_idx = 0; test_idx < test_cmds.size(); test_idx++){
        std::string log_label = "_test_idx_" + std::to_string(test_idx);
        test_logs.push_back(log_dir + "/" + log_prefix + log_label + ".log");
    }

    auto parent_start = std::chrono::system_clock::now();
    std::time_t parent_start_time = std::chrono::system_clock::to_time_t(parent_start);
    log_info(tt::LogTest, "{} - Starting test wrapper for {} tests.", strtok(std::ctime(&parent_start_time), "\n"), test_cmds.size());

    // Do device release+reservations up front, to keep test-dispatching as tight as possible for testing parallelism.
    if (unique_reservation_users_per_test){
        for (int test_idx = 0; test_idx < test_cmds.size(); test_idx++){
            std::string reservation_user = env["USER"].to_string() + std::to_string(test_idx);
            release_and_reserve_mmio_device(1, reservation_user);      // Reserve mmio device
        }
    }

    // Spawn tests
    for (int test_idx = 0; test_idx < test_cmds.size(); test_idx++){
        auto &test = test_cmds.at(test_idx);
        auto &log_file = test_logs.at(test_idx);
        // Try to remove log file, in case it exists from previous run. Otherwise weirdness (stdout appends to end, stderr overwrite start)
        std::remove(log_file.c_str());

        if (unique_reservation_users_per_test){
            std::string reservation_user = env["USER"].to_string() + std::to_string(test_idx);
            env["TT_BACKEND_RESERVATION_USER"] = reservation_user;  // Tell test to use that mmio device
        }

        std::shared_ptr<bp::child> c = std::make_shared<bp::child>(test, env, (bp::std_out & bp::std_err) > log_file);
        log_info(tt::LogTest, "TEST SPAWNED -- {} on pid {} (log: {})", test, c->id(), log_file);
        running_process_map.insert({test, c});
    }

    // Wait for completion
    while (running_process_map.size() > 0) {
        for (auto &test : test_cmds) {
            if (running_process_map.find(test) != running_process_map.end()) {
                std::shared_ptr<bp::child> c = running_process_map.at(test);
                // Some issues here, sometimes quick to fail tests don't update log files..
                if (!c->running()) {
                    c->wait();
                    completed_process_map.insert({test, c});
                    running_process_map.erase(test);
                    std::chrono::duration<double> child_elapsed_seconds = std::chrono::system_clock::now() - parent_start;
                    log_info(tt::LogTest, "TEST COMPLETED -- {} on pid {} (duration: {}s)", test, c->id(), child_elapsed_seconds.count());
                }
            }
        }
    }

    std::vector<int> failing_tests;
    // Check return status
    // return code checking may not be the best way to capture errors
    for (int test_idx = 0; test_idx < test_cmds.size(); test_idx++){
        auto &test = test_cmds.at(test_idx);
        auto &log_file = test_logs.at(test_idx);
        int result =  completed_process_map.at(test)->exit_code();
        if (result != 0){
            pass = false;
            failing_tests.push_back(test_idx);
        }
        if (result) {
            log_error("TEST FAILED -- {} with return code {} (log: {})", test, result, log_file);
        } else {
            log_info(tt::LogTest, "TEST PASSED -- {} (log: {})", test, log_file);
        }
        completed_process_map.erase(test);
    }

    auto parent_end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = parent_end - parent_start;
    std::time_t parent_end_time = std::chrono::system_clock::to_time_t(parent_end);
    log_info(tt::LogTest, "{} - Finished test wrapper for {} tests. Duration was {}s", strtok(std::ctime(&parent_end_time), "\n"), test_cmds.size(), elapsed_seconds.count());

    // For CI/Debug purposes, helpful to show some more info in main log file.
    if (failing_tests.size() > 0){
        log_info(tt::LogTest, "Going to show tail of each failing test's child-process log file now...");
        for (auto &test_idx : failing_tests){
            auto &log_file = test_logs.at(test_idx);
            std::string cmd = "tail -n25 " + log_file;
            std::string cmd_msg = "Running: " + cmd + "\n";
            log_info(tt::LogTest, cmd_msg.c_str());
            auto rt = system(cmd.c_str());
        }
        log_info(tt::LogTest, "Done showing each failing test's child-process logs.\n");
    }

    // Check that no zombie processes remain and exit
    log_assert(running_process_map.size() == 0, "Found zombie process");
    log_assert(completed_process_map.size() == 0, "Found zombie process");
    log_assert(pass, "Overall test FAILED - see per-child output/logs above or snippets (check artifacts dir in CI for full logs).");
    return 0;
}

