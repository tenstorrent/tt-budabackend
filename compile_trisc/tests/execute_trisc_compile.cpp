// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <filesystem>
#include "compile_trisc/compile_trisc.hpp"
#include <experimental/filesystem>
#include <cstdlib>

namespace fs = std::experimental::filesystem;  // see comment above
struct test_config
{
    std::string netlist = "verif/directed_tests/netlist_matmul.yaml";
    std::string arch_name;
    std::string build_path = "build/test/compile_trisc/out";
    bool compile_for_perf_only = false;
    bool trisc_only = false;
    bool fwlog_only = false;
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [--help] [--netlist <netlist>] [--compile-for-perf-only] [--build-path <build-path>] [--trisc-only]" << std::endl;
    std::cout << "  --help: Print this help message" << std::endl;
    std::cout << "  --netlist: Path to the netlist yaml file" << std::endl;
    std::cout << "  --compile-for-perf-only: Only compile for performance" << std::endl;
    std::cout << "  --build-path: Path to the build directory" << std::endl;
    std::cout << "  --trisc-only: Only compile triscs" << std::endl;
    std::cout << "  --fwlog-only: Only tests if fwlog is generated" << std::endl;
}

test_config parseCommandLineArguments(int argc, char* argv[]) {
    test_config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--netlist") {
            if (i + 1 < argc) {
                i++;
                config.netlist = argv[i];
            } else {
                log_fatal("--netlist option requires one argument.");
            }
        } else if (arg == "--compile-for-perf-only") {
            config.compile_for_perf_only = true;
        } else if (arg == "--build-path") {
            if (i + 1 < argc) {
                i++;
                config.build_path = argv[i];
            } else {
                log_fatal("--build_path option requires one argument.");
            }       
        } else if (arg == "--trisc-only") {
            config.trisc_only = true;
        } else if (arg == "--fwlog-only") {
            config.fwlog_only = true;
        } else {
            log_fatal("Unknown option {}", arg);
        }
    }

    return config;
}

void create_file(const std::string& file_name, const std::string& content) {
    std::ofstream file(file_name);
    if (!file.is_open()) {
        log_fatal("Could not open file {}", file_name);
    }
    file << content;
    file.close();
}

void test_ckernel_fwlog_generated(const test_config& config) {
    // make directory
    std::string compile_path = config.build_path + "/fwlog_ckernel_test";
    if (!std::filesystem::exists(compile_path)) {
        std::filesystem::create_directories(compile_path);
    }

    // Simple math kernel program
    std::string tt_log_message = "math_main";
    std::string math_program = "struct hlk_args_t {int i;};";
    math_program += "void math_main(const struct hlk_args_t *args,const int outer_loop_cnt) {";
    math_program += "TT_LOG(\"" + tt_log_message + "{}\",1);";
    math_program += "}";

    // compile dependencies
    create_file(compile_path + "/chlkc_math.cpp", math_program);
    create_file(compile_path + "/kernel_slowdown_config.h", "");
    create_file(compile_path + "/chlkc_math_fidelity.h", "");
    create_file(compile_path + "/chlkc_math_data_format.h", "");
    create_file(compile_path + "/chlkc_math_approx_mode.h", "");
    create_file(compile_path + "/loop_count.h", "constexpr std::int32_t arg_loop_count = 2;");
    create_file(compile_path + "/hlk_args_struct_init.h", "const struct hlk_args_t hlk_args = {.i=0};");

    std::string buda_home = fs::current_path().string();
    perf::PerfDesc perfDesc;
    int thread_id = 1;
    std::string compile_cmd = get_trisc_compile_cmd(buda_home, config.arch_name, perfDesc, compile_path, thread_id /*Math*/);
    std::string link_cmd = get_trisc_link_cmd(buda_home, config.arch_name, compile_path, thread_id /*Math*/);

    // disable tt log
    link_cmd += " ENABLE_TT_LOG=0";
    compile_ckernels_for_trisc(compile_path, thread_id, compile_cmd, link_cmd);

    std::string fwlog = compile_path + "/tensix_thread1/ckernel.fwlog";
    log_assert(std::filesystem::exists(fwlog), "Could not find fwlog {}", fwlog);

    // check file size
    log_assert(std::filesystem::file_size(fwlog) == 0, "fwlog {} is not empty empty", fwlog);

    // enable tt log
    link_cmd += " ENABLE_TT_LOG=1";
    compile_ckernels_for_trisc(compile_path, thread_id, compile_cmd, link_cmd);

    log_assert(std::filesystem::exists(fwlog), "Could not find fwlog {}", fwlog);

    std::ifstream file(fwlog);
    std::string line;
    std::getline(file, line);
    log_assert(line.find(tt_log_message) != std::string::npos, "Could not find {} in fwlog", tt_log_message);
}

void run_test(const test_config& config) {
    if (config.fwlog_only) {
        test_ckernel_fwlog_generated(config);
        return;
    }

    perf::PerfDesc perf_desc;
    log_info(tt::LogTest, "Compiling netlist {}", config.netlist);

    netlist_workload_data workload(config.netlist);
    int num_threads =  3;
    if (config.trisc_only) {        
        generate_trisc_fw(workload, config.arch_name, perf_desc, config.compile_for_perf_only,
            config.build_path, num_threads);
    } else {
        generate_all_fw(workload, config.arch_name, perf_desc, config.compile_for_perf_only,
            config.build_path);
    }

}

int main(int argc, char* argv[]) {
    // print command line arguments
    std::stringstream sstr;
    for (int i = 0; i < argc; ++i) {
        sstr << argv[i] << " ";
    }

    log_info(tt::LogTest, "Test command: {}", sstr.str());

    test_config config = parseCommandLineArguments(argc, argv);

    // create build directory if needed
    if (!std::filesystem::exists(config.build_path)) {
        std::filesystem::create_directories(config.build_path);
    }

    if (std::getenv("ARCH_NAME") == nullptr) {
        log_fatal("ARCH_NAME environment variable not set");
    }

    config.arch_name = std::getenv("ARCH_NAME");

    run_test(config);

    return 0;
}