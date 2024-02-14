// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "netlist_analyzer.hpp"

#include <filesystem>
namespace fs = std::filesystem;

struct test_args {
    std::string netlist_path = "";
    bool net2pipe_flow = false;
    std::string arch = "";
    std::string cluster_file = "";
    std::string out_dir = "";
    bool help = false;
};
namespace {
inline std::tuple<std::string, std::vector<std::string>> get_command_option_and_remaining_args(
    const std::vector<std::string> &verif_args,
    const std::string &option,
    const std::optional<std::string> &default_value = std::nullopt) {
    std::vector<std::string> remaining_args = verif_args;
    std::vector<std::string>::const_iterator option_pointer =
        std::find(std::begin(remaining_args), std::end(remaining_args), option);
    if (option_pointer != std::end(remaining_args) and (option_pointer + 1) != std::end(remaining_args)) {
        std::string value = *(option_pointer + 1);
        remaining_args.erase(option_pointer, option_pointer + 2);
        return {value, remaining_args};
    }
    if (not default_value.has_value()) {
        throw std::runtime_error("Option not found!");
    }
    return {default_value.value(), remaining_args};
}
inline std::tuple<bool, std::vector<std::string>> has_command_option_and_remaining_args(
    const std::vector<std::string> &verif_args, const std::string &option) {
    std::vector<std::string> remaining_args = verif_args;
    std::vector<std::string>::const_iterator option_pointer =
        std::find(std::begin(remaining_args), std::end(remaining_args), option);
    bool value = option_pointer != std::end(remaining_args);
    if (value) {
        remaining_args.erase(option_pointer);
    }
    return {value, remaining_args};
}
inline void validate_remaining_args(const std::vector<std::string> &remaining_args) {
    if (remaining_args.size() == 1) {
        // Only executable is left, so all good
        return;
    }
    std::cout << "Remaining verif_args:" << std::endl;
    for (unsigned int i = 1; i < remaining_args.size(); i++) {
        std::cout << "\t" << remaining_args.at(i) << std::endl;
    }
    throw std::runtime_error("Not all verif_args were parsed");
}
}

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--run-net2pipe              : Runs the net2pipe flow\n";
    help_string += "--arch                      : grayskull, wormhole_b0\n";
    help_string += "--cluster-desc              : Specifies the path to the cluster descriptor yaml\n";
    help_string += "--out-dir                   : Specifies the base output directory\n";
    help_string += "--help                      : Prints this message\n";

    try {
        std::tie(args.netlist_path, input_args) =
            ::get_command_option_and_remaining_args(input_args, "--netlist", "netlist_analyzer/tests/netlists/test_dram_unary_dram.yaml");
        std::tie(args.net2pipe_flow, input_args) = ::has_command_option_and_remaining_args(input_args, "--run-net2pipe");
        std::tie(args.arch, input_args) = ::get_command_option_and_remaining_args(input_args, "--arch", "grayskull");
        std::tie(args.cluster_file, input_args) = ::get_command_option_and_remaining_args(input_args, "--cluster-desc", "");
        std::tie(args.out_dir, input_args) = ::get_command_option_and_remaining_args(input_args, "--out-dir", "");
        std::tie(args.help, input_args) = ::has_command_option_and_remaining_args(input_args, "--help");
        ::validate_remaining_args(input_args);
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

    if (!run_command(net2pipe_cmd.str(), net2pipe_log, false)) {
        log_fatal("Running net2pipe command failed: {}", net2pipe_cmd.str());
    }
}
}

int main(int argc, char** argv) {
    std::vector<std::string> input_args(argv, argv + argc);
    test_args args = parse_test_args(input_args);

    fs::path test_yaml(args.netlist_path);
    const string analyzer_output_dir_path = (args.out_dir == "") ? "analyzer_out/" + std::string(test_yaml.stem()) : args.out_dir;
     
    log_info(tt::LogAnalyzer, "Output dir: {}", analyzer_output_dir_path);
    if (!fs::exists(analyzer_output_dir_path + "/netlist_analyzer/")) {
        fs::create_directories(analyzer_output_dir_path + "/netlist_analyzer/");
    }

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

    auto netlist_analyzer = tt_netlist_analyzer(args.arch, args.netlist_path, args.cluster_file);
    
    netlist_analyzer.run_net2pipe_flow(analyzer_output_dir_path);

    return 0;
}

