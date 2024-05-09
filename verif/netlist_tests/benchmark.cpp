// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>
#include <chrono>
#include <ctime>

#include "test_training.hpp"
#include "test_inference.hpp"
#include "tt_backend.hpp"
#include "runtime.hpp"
#include "verif.hpp"
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

const string BINARIES_WEKA_DIR_PATH = std::getenv("TT_BACKEND_CI_TEST_BINARIES_DIR") ? 
                    std::getenv("TT_BACKEND_CI_TEST_BINARIES_DIR") : "/proj_sw/ci/software/spatial2/backend/binaries/CI_PERF_TEST_BINARIES/";

struct common_test_args {
    int backend_opt_level = 0;
    int loop_count = 1;
    bool help = false;
};

struct test_args : common_test_args {
    string model = "bert";
    string config = "";
    bool training = false;
    string data_format_str = "bfp8b";
    string math_fidelity_str = "lofi";
    int microbatch = -1;
    int chips = 1;
    perf::PerfTraceMode trace = perf::PerfTraceMode::None;
    string perf_trace_str = "none";
    string pybuda_commit_hash = "";
    string arch_str = "grayskull";
    string perf_output_dir = "";
    uint version = 1;
    string date = "";
    bool external_binaries = false;
};

string flatten_input_args(vector<string> input_args) {
    string input_args_str = "";
    for (auto arg: input_args) {
        input_args_str += arg + " ";
    }
    return input_args_str;
}

DataFormat get_data_format_from_string(const std::string &data_format_str) {
    DataFormat data_format;
    if (data_format_str == "bfp8") {
        data_format = DataFormat::Bfp8;
    } else if (data_format_str == "bfp8b") {
        data_format = DataFormat::Bfp8_b;
    } else if (data_format_str == "fp16") {
        data_format = DataFormat::Float16;
    } else if (data_format_str == "fp16b") {
        data_format = DataFormat::Float16_b;
    } else {
        throw std::invalid_argument("Unknown format");
    }
    return data_format;
}

MathFidelity get_math_fidelity_from_string(const std::string &math_fidelity_str) {
    MathFidelity math_fidelity;
    if (math_fidelity_str == "lofi") {
        math_fidelity = MathFidelity::LoFi;
    } else if (math_fidelity_str == "hifi2") {
        math_fidelity = MathFidelity::HiFi2;
    } else if (math_fidelity_str == "hifi3") {
        math_fidelity = MathFidelity::HiFi3;
    } else if (math_fidelity_str == "hifi4") {
        math_fidelity = MathFidelity::HiFi4;
    } else {
        throw std::invalid_argument("Unknown format");
    }
    return math_fidelity;
}

std::string str_lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),  [](unsigned char c){ return std::tolower(c); });
    return s;
}

test_args parse_test_args(std::vector<std::string> &input_args) {
    test_args args;
    string help_string;
    help_string += "--model                                     : The test model to run. e.g: bert\n";
    help_string += "--config                                    : The test model config to run. e.g: tiny\n";
    help_string += "--training                                  : Run the model in training mode. If false, run in inference mode\n";
    help_string += "--data-format                               : Data format to run the model with. e.g: Fp16b\n";
    help_string += "--math-fidelity                             : Math fidelity to run the model with. e.g. HiFi3\n";
    help_string += "--O                                         : Backend opt level\n";
    help_string += "--num-loops                                 : Number of loops of input_counts to run (Default: 1)\n";
    // help_string += "--microbatch                                : The microbatch size\n";
    // help_string += "--chips                                     : The number of chips to use\n";
    help_string += "--trace                                     : The performance trace level\n";
    help_string += "--version                                   : The version of the perf test\n";
    // help_string += "--pybuda-commit-hash                        : The pybuda commit hash that the netlist was generated with\n";
    help_string += "--help                                      : Prints this message\n";
    try {
        // Shared section of the input args
        // Do not remove them from input_args
        vector<string> temp_vec;
        std::tie(args.loop_count, temp_vec) = verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.backend_opt_level, temp_vec) = verif_args::get_command_option_int32_and_remaining_args(input_args, "--O", 2); // default to highest level
        std::tie(args.help, temp_vec) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        std::tie(args.perf_output_dir, temp_vec) = verif_args::get_command_option_and_remaining_args(input_args, "--perf-output-dir", std::string(""));

        // Args unique to the wrapper test
        std::tie(args.model, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--model", "bert");
        std::tie(args.config, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--config", "");
        std::tie(args.training, input_args) = 
            verif_args::has_command_option_and_remaining_args(input_args, "--training");
        
        // string data_format_str;
        // string math_fidelity_str;
        std::tie(args.data_format_str, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--data-format", "bfp8b");
        std::tie(args.math_fidelity_str, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--math-fidelity", "lofi");
        
        DataFormat data_format = get_data_format_from_string(args.data_format_str);
        MathFidelity math_fidelity = get_math_fidelity_from_string(args.math_fidelity_str);
        
        // Currently not supported
        
        // std::tie(args.microbatch, input_args) =
        //     verif_args::get_command_option_int32_and_remaining_args(input_args, "--microbatch", 1);
        // std::tie(args.chips, input_args) =
        //     verif_args::get_command_option_int32_and_remaining_args(input_args, "--chips", 1);
        // std::tie(args.pybuda_commit_hash, input_args) =
        //     verif_args::get_command_option_and_remaining_args(input_args, "--pybuda-commit-hash", "");
        
        std::tie(args.version, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--version", 1);
        std::tie(args.date, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--date", "");

        std::tie(args.perf_trace_str, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--trace", "none");
        
        std::tie(args.external_binaries, input_args) = 
            verif_args::has_command_option_and_remaining_args(input_args, "--external-binaries");
        
        args.trace =    (args.perf_trace_str == "light")     ? perf::PerfTraceMode::Light    :
                        (args.perf_trace_str == "verbose")   ? perf::PerfTraceMode::Verbose  : perf::PerfTraceMode::None;
        
        std::tie(args.arch_str, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--arch", "grayskull");
    
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

string get_netlist_prefix(const test_args &args) {
    string netlist_name = "";
    netlist_name += args.model + "_";
    if (args.config != "") {
        netlist_name += args.config + "_";
    }
    netlist_name += args.math_fidelity_str + "_";
    netlist_name += args.data_format_str;
    if (args.version > 1) {
        netlist_name += "_v" + to_string(args.version);
    }
    if (args.date != "") {
        netlist_name += "_" + args.date;
    }
    return netlist_name;
}

string get_netlist_path(const test_args &args) {
    string netlist_path = "perf_lib/graph_tests/";
    string arch_str_lower = str_lowercase(args.arch_str);
    log_assert((arch_str_lower == "grayskull") || (arch_str_lower == "wormhole") || (arch_str_lower == "wormhole_b0"), "Invalid arch");
    
    netlist_path += arch_str_lower + "/";
    netlist_path += args.training ? "training/" : "inference/";
    
    string netlist_name = get_netlist_prefix(args);
    netlist_name += ".yaml";
    
    netlist_path += netlist_name;
    if (!fs::exists(netlist_path)) {
        log_fatal("Following netlist path that was generated from test config, does not exist: {}", netlist_path);
    }
    log_info(tt::LogTest, "Using netlist {}", netlist_path);
    return netlist_path;
}

string get_binaries_path(const test_args &args) {
    
    string arch_str_lower = str_lowercase(args.arch_str);
    log_assert((arch_str_lower == "grayskull") || (arch_str_lower == "wormhole") || (arch_str_lower == "wormhole_b0"), "Invalid arch");
    
    string binaries_path = BINARIES_WEKA_DIR_PATH;
    binaries_path += arch_str_lower + "/";
    binaries_path += args.training ? "training/" : "inference/";

    string netlist_prefix = get_netlist_prefix(args);
    binaries_path += netlist_prefix + "/";
    binaries_path += "binaries/";
    if (!fs::exists(binaries_path)) {
        log_fatal("Following binary path that was generated from test config, does not exist: {}", binaries_path);
    }
    log_info(tt::LogTest, "Using tensor binaries under path {}", binaries_path);

    return binaries_path;
}

void append_extra_configs_to_input_arguments(std::vector<std::string> &input_args, const test_args &args, const string &netlist_path) {
    
    log_assert(perf::perf_trace_mode_to_perf_config.find(args.trace) != perf::perf_trace_mode_to_perf_config.end(), "Could not find perf trace mode");
    
    vector<string> perf_args = perf::perf_trace_mode_to_perf_config.at(args.trace);
    
    input_args.insert(input_args.end(), perf_args.begin(), perf_args.end());

    input_args.push_back("--netlist");
    input_args.push_back(netlist_path);

    if (args.external_binaries) {
        input_args.push_back("--tensor-binary-dir");
        string binary_path = get_binaries_path(args);
        input_args.push_back(binary_path);
        input_args.push_back("--read-tensor-binaries");
        input_args.push_back("constants");
    }

    cout << "input args to the tests = " << endl;
    for (auto arg: input_args) {
        cout << arg << ", ";
    }
    cout << endl;
}

void create_test_config_file(test_args args) {
    
    if (args.perf_output_dir == "") {
        log_warning(tt::LogTest, "Test output config will not be generated since perf_output_dir override is not specified");
        return;
    }
    YAML::Node config_file;
    config_file["args.model"]               = args.model;
    config_file["args.config"]              = args.config;
    config_file["args.training"]            = args.training;
    config_file["args.dataformat"]          = args.data_format_str;
    config_file["args.math_fidelity"]       = args.math_fidelity_str;
    config_file["args.backend_opt_level"]   = args.backend_opt_level;
    config_file["args.loop_count"]          = args.loop_count;
    config_file["args.microbatch"]          = args.microbatch;
    config_file["args.chips"]               = args.chips;
    config_file["args.trace"]               = args.perf_trace_str;
    config_file["args.pybuda_commit_hash"]  = args.pybuda_commit_hash;

    log_assert(fs::exists(args.perf_output_dir), "Perf putput dir does not exist");
    log_info(tt::LogTest, "Creating test config yaml file under {}", args.perf_output_dir + "/test_config.yaml");
    std::ofstream output_file(args.perf_output_dir + "/test_config.yaml");
    output_file << config_file;
}

int main(int argc, char **argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in benchmark since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    bool pass = true;
    int return_code = 0;
    std::vector<std::string> input_args(argv, argv + argc);
    string input_args_flattened = flatten_input_args(input_args);
    test_args args = parse_test_args(input_args);
    string netlist_path = get_netlist_path(args);
    append_extra_configs_to_input_arguments(input_args, args, netlist_path);

    try {
        if (args.training) {
            return_code = training::run(input_args);
        } else {
            return_code = inference::run(input_args);
        }
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        pass = false;
    }

    create_test_config_file(args);

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Passed");
    }
    return return_code;
}
