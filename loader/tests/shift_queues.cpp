// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <vector>
#include <fstream>
#include "verif/verif.hpp"
#include <filesystem>


int main(int argc, char** argv){
    std::vector<std::string> args(argv, argv + argc);
    std::string input_filepath = "";
    std::string output_filepath = "";
    int offset = 0;

    std::tie(input_filepath, args) = verif_args::get_command_option_and_remaining_args(args, "--input-netlist");
    std::tie(output_filepath, args) = verif_args::get_command_option_and_remaining_args(args, "--output-netlist");
    std::tie(offset, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--offset");

    log_assert(std::filesystem::exists(input_filepath), "Input netlist file does not exist");
    log_assert(offset >= 0, "Offset to shift queue addresses must be greater than or equal to 0");
    log_assert(!(offset % 32), "Offset must be a multiple of 32 to ensure correct alignemnt of DRAM queues in the output netlist.");
    verif::shift_addresses_by_offset(input_filepath, output_filepath, offset);
    return 0;
}