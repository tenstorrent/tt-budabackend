// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "verif.hpp"

bool checkForLeaks(const std::string& filePath) {
    std::ifstream file(filePath);

    if (!file.is_open()) {
        log_assert(!file.is_open(), "Failed to open file: {}", filePath);
        return false;
    }
    std::vector<std::string> mem_leak_types = {"definitely lost:", "indirectly lost:", "possibly lost:"};
    std::string line;
    bool leak_found = false;
    while (std::getline(file, line)) {
        for(auto& error_msg : mem_leak_types) {
            if (line.find(error_msg) != std::string::npos) {
                size_t startPos = line.find(":") + 1;
                size_t endPos = line.find(" bytes");
                std::string bytesStr = line.substr(startPos, endPos - startPos);
                int bytes = std::stoi(bytesStr);
                if (bytes > 0) {
                    log_warning(tt::LogTest, "Possible Memory Leak Detected: {} {} bytes", error_msg, bytes);
                    leak_found = true;
                }
            }
        }
    }
    log_info(tt::LogTest, "No memory leaks detected.");
    return leak_found;
}

int main(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);
    std::string test_cmd = "";
    std::tie(test_cmd, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--test-cmd", "");

    test_cmd = "valgrind --leak-check=full --log-file=\"temp.log\" " + test_cmd;
    log_info(tt::LogTest, "Running Command: {}", test_cmd);
    int ret_code = system(test_cmd.c_str());
    log_assert(ret_code == 0, "Test command failed to run with Valgrind. Could not check for memory leaks.");

    bool hasLeaks = checkForLeaks("temp.log");
    log_assert(!hasLeaks, "Memory Leak checking failed with the messages above!");
}