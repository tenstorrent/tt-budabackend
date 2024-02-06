// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// File: test_errors.cpp
#include <fstream>

#include "netlist_parser.hpp"
#include "utils.hpp"
#include "verif.hpp"

using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

constexpr int EXIT_OK = 0;
constexpr int EXIT_TEST_FAILED = 1;
constexpr int EXIT_INVALID_ARGS = 2;


struct test_args
{
    std::string netlist_path = "";
    bool help = false;
};

test_args parse_test_args(std::vector<std::string> input_args)
{
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--help         : Prints this message\n";

    try
    {
        std::tie(args.netlist_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, "--netlist", "verif/error_tests/netlists/error_netlist.yaml");
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        verif_args::validate_remaining_args(input_args);
    }
    catch (const std::exception& e)
    {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(EXIT_INVALID_ARGS);
    }

    if (args.help)
    {
        log_info(tt::LogNetlist, "Usage Help:\n{}", help_string);
        exit(EXIT_OK);
    }
    return args;
}

// Test expects error to occur during netlist parsing.
bool test_parser_error(const std::string& netlist_path)
{
    try
    {
        netlist_parser parser;
        parser.parse_file(netlist_path);
        log_info(tt::LogTest, "Error didn't occure.");
    }
    catch(const std::runtime_error& error)
    {
        return true;
    }

    return false;
}

int main(int argc, char** argv)
{
    // Get Test parameters
    std::vector<std::string> input_args(argv, argv + argc);
    log_info(tt::LogTest, "test_errors");
    test_args args = parse_test_args(input_args);
    
    bool pass = test_parser_error(args.netlist_path);
    
    if (pass)
    {
        log_info(tt::LogTest, "Test Passed");
    }
    else
    {
        log_info(tt::LogTest, "Test Failed");
    }

    return pass ? EXIT_OK : EXIT_TEST_FAILED;
}
