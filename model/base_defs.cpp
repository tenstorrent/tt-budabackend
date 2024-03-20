// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/base_defs.h"

#include <cstdlib>
#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::experimental::filesystem; // see comment above

using namespace std;

namespace tt {
bool run_command(const string &cmd, const string &log_file) {
    cmd_result_t result = run_command(cmd, log_file, "");
    return result.success;
}

cmd_result_t run_command(const string &cmd, const string &log_file, const string &err_file) {
    bool verbose = getenv("TT_BACKEND_DUMP_RUN_CMD") != nullptr;
    string redirected_cmd = cmd;
    if (verbose) {
        std::cout << "running: `" + redirected_cmd + '`' << std::endl;
    } else {
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
}