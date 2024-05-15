// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/base_defs.h"
#include <cstdlib>
#include <fstream>
#include <thread>
#include <boost/process.hpp>

namespace  bp = boost::process;
namespace tt {

using std::string;

bool run_command(const string &cmd, const string &log_file) {
    cmd_result_t result = run_command(cmd, log_file, "");
    return result.success;
}

cmd_result_t run_command(const string &cmd, const string &log_file, const string &err_file) {
    bool verbose = getenv("TT_BACKEND_DUMP_RUN_CMD") != nullptr;
    string redirected_cmd = cmd;
    if (verbose) {
        std::cout << "running: `" + redirected_cmd + '`' << std::endl;
    } 
    // Redirect stdout to a file if requested
    if (log_file != "") {
        redirected_cmd += " >> " + log_file;
    }
    // Redirect stderr to a file if requested
    if (err_file != "") {
        redirected_cmd += " 2>> " + err_file;
    }

    bool success = (system(redirected_cmd.c_str()) == 0);
    if (success or err_file == "")
        return {success, ""};

    // Read the contents of the error log file
    std::ifstream file(err_file);
    std::string err_message((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return {success, err_message};
}

cmd_result_t run_command_with_timeout(const string &cmd, const std::chrono::seconds &timeout_seconds, const string &log_file, const string &err_file, bool wait_until_done) {
    bool verbose = getenv("TT_BACKEND_DUMP_RUN_CMD") != nullptr;
    if (verbose) {
        std::cout << "running: `" + cmd + '`' << " with timeout " << timeout_seconds.count() << " second(s)" << std::endl;
    }
    const auto &env = boost::this_process::environment();
    std::unique_ptr<bp::child> c;
    if (log_file == "" and err_file == "") {
        c = std::make_unique<bp::child>(cmd, env);
    }
    else if (log_file != "" and err_file == "") {
        c = std::make_unique<bp::child>(cmd, env, bp::std_out > log_file);
    }
    else if (log_file == "" and err_file != "") {
        c = std::make_unique<bp::child>(cmd, env, bp::std_err > err_file);
    }
    else {
        c = std::make_unique<bp::child>(cmd, env, bp::std_out > log_file, bp::std_err > err_file);
    }

    // wait for timeout_seconds or the command to finish, whichever comes first
    if (c->wait_for(timeout_seconds)) {
        return {.success=true, .message=""};
    }
    else {
        // force kill the process if not wait_until_done
        wait_until_done ? c->wait() : c->terminate();
        const string err_message = "Running cmd " + cmd + " exceeded timeout of " + std::to_string(timeout_seconds.count()) + " second(s).";
        return {.success=false, .message=err_message};
    }
}

cmd_result_t run_function_with_timeout(const std::function<void()> &f, const std::chrono::seconds &timeout_seconds, bool wait_until_done) {
    bool done = false;
    std::exception_ptr eptr;
    std::thread worker_thread([&]() {
        try {
            f();
        }
        catch (...) {
            eptr = std::current_exception();
        }
        done = true;
    });
    std::chrono::seconds elapsed_seconds;
    std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time);
        if (elapsed_seconds > timeout_seconds) {
            if (wait_until_done) {
                worker_thread.join();
            }
            if (eptr) {
                std::rethrow_exception(eptr);
            }
            const string error_msg = "Running function exceeded timeout of " + std::to_string(timeout_seconds.count()) + " second(s).";
            return {.success=false, .message=error_msg};
        }
    }
    worker_thread.join();
    if (eptr) {
        std::rethrow_exception(eptr);
    }
    return {.success=true, .message=""};
}


}