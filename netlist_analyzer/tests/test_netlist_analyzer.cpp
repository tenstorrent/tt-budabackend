// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "netlist_analyzer.hpp"

#include "verif/verif_args.hpp"

#include <filesystem>
namespace fs = std::filesystem;

//#include "runtime/runtime_utils.hpp"

struct test_args {
    std::string netlist_path = "";
    bool net2pipe_flow = false;
    std::string arch = "";
    std::string cluster_file = "";
    bool help = false;
};

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--run-net2pipe              : Runs the net2pipe flow\n";
    help_string += "--arch                      : grayskull, wormhole_b0\n";
    help_string += "--help                      : Prints this message\n";
    try {
        std::tie(args.netlist_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--netlist", "netlist_analyzer/tests/netlists/test_dram_unary_dram.yaml");
        std::tie(args.net2pipe_flow, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--run-net2pipe");
        std::tie(args.arch, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--arch", "grayskull");
        std::tie(args.cluster_file, input_args) = verif_args::get_command_option_and_remaining_args(input_args, "--cluster-desc", "");
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        verif_args::validate_remaining_args(input_args);
    } catch (const std::exception& e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (args.help) {
        log_info(tt::LogTest, "Usage Help:\n{}", help_string);
        exit(0);
    }
    return args;
}

namespace {

bool run_command(const string &cmd, const string &log_file, const bool verbose) {
    int ret;
    //if (getenv("TT_BACKEND_DUMP_RUN_CMD") or verbose) {
    //    cout << "running: `" + cmd + '`';
    //    ret = system(cmd.c_str());
    //} else {
        string redirected_cmd = cmd + " > " + log_file;
        ret = system(redirected_cmd.c_str());
    //}
    return (ret == 0);
}

void generate_pipegen_spec(const string &netlist, const string &build_dir_path, const string &soc_descriptor_path, const string &network_desc_path) {
    //PROFILE_SCOPE_MS();
    string root;
    if (getenv("BUDA_HOME")) {
        root = getenv("BUDA_HOME");
    } else {
        root = "./";
    }
    if (root.back() != '/') {
        root += "/";
    }
    ///bool verbose = getenv("PRINT_PIPEGEN");
    stringstream net2pipe_cmd;
    string net2pipe_path = "build/bin/net2pipe ";
    string net2pipe_log = build_dir_path + "/net2pipe.log";

    /*
    net2pipe <input netlist yaml> <output directory> <global epoch start> <soc descriptor yaml> <(optional)cluster descriptor file>
    */
    net2pipe_cmd << root << net2pipe_path;
    net2pipe_cmd << " " << netlist;
    net2pipe_cmd << " " << build_dir_path;
    net2pipe_cmd << " " << "0";
    net2pipe_cmd << " " << soc_descriptor_path;
    net2pipe_cmd << " " << network_desc_path;

    stringstream net2pipe_out;
    net2pipe_out << "Run net2pipe" << endl << net2pipe_cmd.str();
    log_info(tt::LogRuntime, "{}", net2pipe_out.str());

    if (!fs::exists(build_dir_path)) {
        fs::create_directories(build_dir_path);
    }
    if (!run_command(net2pipe_cmd.str(), net2pipe_log, false)) {
        log_fatal("Running net2pipe command failed: {}", net2pipe_cmd.str());
    }
}
}

int main(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);
    test_args args = parse_test_args(input_args);

    fs::path test_yaml(args.netlist_path);
    const string analyzer_output_dir_path = "analyzer_out/" + std::string(test_yaml.stem());

    if(args.net2pipe_flow) {
        log_info(tt::LogTest, "Running net2pipe flow");
        std::string arch_string;
        if (args.arch == "grayskull") {
            arch_string = "device/grayskull_10x12.yaml";
        }
        else if (args.arch == "wormhole_b0") {
            arch_string = "device/wormhole_b0_8x10.yaml";
        }
        else {
            log_fatal("Unknown arch: {}, Valid Archnames: [grayskull, wormhole_b0]", args.arch);
        }

        ::generate_pipegen_spec(args.netlist_path, analyzer_output_dir_path, arch_string, args.cluster_file);
    }

    auto netlist_analyzer = tt_netlist_analyzer(args.arch, args.netlist_path);
    if(args.net2pipe_flow) {
        netlist_analyzer.run_net2pipe_flow(analyzer_output_dir_path);
    }
    else {
        netlist_analyzer.run_default_flow();
    }

    return 0;
}

