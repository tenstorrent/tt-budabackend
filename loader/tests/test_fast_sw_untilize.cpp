// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <chrono>
#include <ctime>

#include "tt_backend.hpp"
#include "runtime.hpp"
#include "utils.hpp"
#include "verif.hpp"
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

struct test_args {
    std::string netlist_path = "";
    std::string output_dir = "";
    int seed = -1;
    std::string backend = "";
    std::vector<std::string> input_names = {};
    std::vector<std::string> output_names = {};
    bool help = false;
    std::string random_type_str = "none";
    bool run_only = false;
    bool skip_tile_check = false;
    int num_loops = 1;
};
enum class RandomType {
    None = 0,
    EntireValueNormal = 1,
    EntireValueUniform = 2,
    EntireValueNormalLargeSTD = 3,
    EachPortionNormal = 4,
    Diagonal = 5,
    EntireValueNormalBiasedPos = 6,
    EntireValueNormalBiasedNeg = 7,
    EntireValueNormalSmallVals = 8,
};

const map<std::string, RandomType> arg_to_rand_type {
    {"none", RandomType::None},
    {"normal", RandomType::EntireValueNormal},
    {"normal-large-stddev", RandomType::EntireValueNormalLargeSTD},
    {"normal-bias-pos", RandomType::EntireValueNormalBiasedPos},
    {"normal-bias-neg", RandomType::EntireValueNormalBiasedNeg},
    {"diagonal", RandomType::Diagonal},
    {"uniform", RandomType::EntireValueUniform},
    {"normal-each-portion", RandomType::EachPortionNormal},
    {"normal-small-values", RandomType::EntireValueNormalSmallVals},
};

RandomType get_random_config (string rand_arg) {
    log_assert(arg_to_rand_type.find(rand_arg) != arg_to_rand_type.end(), "Could not find args");
    return arg_to_rand_type.at(rand_arg);
}

tt_tensor get_tilized_tensor(tt_tensor_metadata md, RandomType random_type) {
    setenv("TT_BACKEND_FORCE_SLOW_UNTILIZE", "1", 1); //Set this here to invoke slow unpack (baseline)
    tt_tensor tensor(md);
    if (random_type == RandomType::EachPortionNormal) {
        int spread = 127;
        int man_variance_bits = 23;
        tensor.randomize_manual_float(spread, man_variance_bits);
    } else if (random_type == RandomType::EntireValueNormal) {
        float mean = 0.0;
        float stddev = 0.25;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::EntireValueNormalLargeSTD) {
        float mean = 0.0;
        float stddev = 1000000.0;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::EntireValueUniform) {
        float lower_bound = -1000000.0;
        float upper_bound = 1000000.0;
        tensor.randomize_uniform(lower_bound, upper_bound);
    } else if (random_type == RandomType::Diagonal) {
        float mean = 0.0;
        float stddev = 1000000.0;
        tensor.randomize_diagonal(mean, stddev);
    } else if (random_type == RandomType::EntireValueNormalBiasedPos) {
        float mean = 10000.0;
        float stddev = 10000.0;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::EntireValueNormalBiasedNeg) {
        float mean = -10000.0;
        float stddev = 10000.0;
        tensor.randomize(mean, stddev);
    } else if (random_type == RandomType::None) {
        tensor.init_to_tile_id(1.0f, 0);
    } else if (random_type == RandomType::EntireValueNormalSmallVals) {
        float mean = 0.0;
        float stddev = 0.001;
        tensor.randomize(mean, stddev);
    } else {
        log_assert(false, "Invalid random type.");
    }
    tensor.pack_data();
    tensor.unpack_data();
    return tensor;
}


test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--outdir <>                 : Custom output dir path, must be provided for run-only to locate pre-compiled objects\n";
    help_string += "--seed <>                   : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>                : Path to netlist file\n";
    help_string += "--backend                   : Target backend to run to (Default: Silicon)\n";
    help_string += "--inputs                    : Host inputs to device\n";
    help_string += "--run-only                  : Skip compile, just run only\n";
    help_string += "--num-loops                 : Run test N times in a loop\n";
    help_string += "--help                      : Prints this message\n";
    try {
        std::tie(args.output_dir, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outdir", "");
        if (args.output_dir == "") {
            // log_assert(!args.run_only, "RunOnly test must be provided an output dir with precompiled objects!");
            args.output_dir = verif_filesystem::get_output_dir(__FILE__, input_args);
        }
        std::string cmdline_arg;
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.netlist_path, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--netlist", "netlist.yaml");
        std::tie(args.backend, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--backend", "Silicon");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--inputs", "");

        tt::args::split_string_into_vector(args.input_names, cmdline_arg, ",");
        std::tie(cmdline_arg, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outputs", "");

        tt::args::split_string_into_vector(args.output_names, cmdline_arg, ",");
         std::tie(args.random_type_str, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--randomize-tensor", "none");
        std::tie(args.run_only, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--run-only");
        std::tie(args.skip_tile_check, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--skip-tile-check");

        std::tie(args.num_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);

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

int run(std::vector<std::string> &input_args)
{
    bool pass = true;
    test_args args = parse_test_args(input_args);
    RandomType random_type = get_random_config(args.random_type_str);
    auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(start);
    log_info(tt::LogTest, "{} - Starting test for netlist {}.", strtok(std::ctime(&start_time), "\n"), args.netlist_path);

    // Randomization seeding
    if (args.seed == -1) {
        args.seed = tt_gen_seed();
        log_info(tt::LogTest, "Unspecified cmdline --seed , generated random seed {}", args.seed);
    }
    tt_rnd_set_seed(args.seed);

    // ----------------------------------------------------------------
    // Test configurations
    // ----------------------------------------------------------------
    netlist_workload_data workload(args.netlist_path);

    // Test configs for stimulus and comparison
    auto config = verif::test_config::read_from_yaml_file(args.netlist_path, "verif/netlist_tests/default_test_config.yaml");

    // Append netlist config to test args
    args.input_names.insert(args.input_names.end(), config.io_config["inputs"].begin(), config.io_config["inputs"].end());
    args.output_names.insert(args.output_names.end(), config.io_config["outputs"].begin(), config.io_config["outputs"].end());

    const int microbatch_count = verif::test_config::get<int>(config.test_args, "microbatch_count", 1);
 
    std::map<std::string, std::string> program_parameters = {
        {"$p_microbatch_count", std::to_string(microbatch_count)},
        {"$p_loop_count", std::to_string(microbatch_count)},
    };


    tt_backend_config target_config = {
        .type = get_device_from_string(args.backend),
        .arch = workload.device_info.arch,
        .output_dir=args.output_dir,
    };

    if (args.run_only) {
        target_config.mode = DEVICE_MODE::RunOnly;
    }

    std::shared_ptr<tt_backend> target_backend;
    if (target_config.type != tt::DEVICE::Invalid) {
        target_backend = tt_backend::create(args.netlist_path, target_config);
    }

    if (target_config.type != tt::DEVICE::Invalid) {
        log_assert(target_backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected backend to be initialized succesfuly");
    }

    for (const std::string &input_name : args.input_names) {
        log_assert(workload.queues.find(input_name) != workload.queues.end(), "Specified name of an input queue {} which isn't a name of any queue in the netlist", input_name);
    }
    for (const std::string &output_name : args.output_names) {
        log_assert(workload.queues.find(output_name) != workload.queues.end(),  "Specified name of an output queue {} which isn't a name of any queue in the netlist", output_name);
    }

    std::vector<std::string> init_io_names = {};
    for (auto &io : workload.queues) {
        tt_queue_info &q_info = io.second.my_queue_info;
        if (q_info.input == "HOST") {
            init_io_names.push_back(q_info.name);
        }
    }

   
    std::set<std::string> input_names(args.input_names.begin(), args.input_names.end());
    log_assert(input_names.size() > 0 , "input_names must not be empty!");

    for (int loop=0; loop<args.num_loops; loop++) {
        log_info(tt::LogTest, "Running loop: {} of num_loops: {} in test now...", loop+1, args.num_loops);

        for (const auto &io_name : init_io_names) {
            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(io_name);
            tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
            tt_queue_info &queue_info = workload.queues.at(q_desc.queue_name).my_queue_info;

            tt_tensor io_tensor;
            io_tensor = get_tilized_tensor(md, random_type);
            if (target_config.type != tt::DEVICE::Invalid) {
                verif::push_tensor(*target_backend, io_name, &io_tensor, 0);
            }
        }

        for (std::string program_name : workload.program_order) {
            if (target_config.type != tt::DEVICE::Invalid) {
                log_assert(target_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to be run successfully");
            }
        }

        std::set<std::string> output_names(args.output_names.begin(), args.output_names.end());
        log_assert(output_names.size() > 0 , "output_names must not be empty!");
        
        for (const auto &output_name : output_names) {
            if (workload.queues.find(output_name) != workload.queues.end()) {
                tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
                tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
                log_info(tt::LogTest, "Getting and Popping tensors from for output_name: {}", output_name);
                std::vector<tt_tensor> observed;
                std::vector<tt_tensor> expected;
                if (target_config.type != tt::DEVICE::Invalid) {

                    if (args.skip_tile_check) {
                        verif::get_and_pop_tensors(*target_backend, output_name, expected, -1, 1);
                    } else {

                        log_info(tt::LogTest, "Comparing Slow vs Fast untilized data");
                        unsetenv("TT_BACKEND_FORCE_SLOW_UNTILIZE");
                        verif::get_and_pop_tensors(*target_backend, output_name, observed, -1, 1, false);
                        setenv("TT_BACKEND_FORCE_SLOW_UNTILIZE", "1", 1);
                        verif::get_and_pop_tensors(*target_backend, output_name, expected, -1, 1);

                        for (int input = 0; input < observed.size(); input++) {
                            log_info(tt::LogTest, "Checking Entry idx={} for output={}", input, output_name);
                            if (not compare_tensors(expected.at(input), observed.at(input), verif::comparison::get_config(output_name, config.comparison_configs))) {
                                pass = false;
                                log_error("Entry idx={} for output={} Mismatched", input, output_name);
                            }
                        }
                    }
                }
            }
            else {
                pass = false;
                log_error("Cannot find output queue with name {}", output_name);
            }
        }

    }
        
    if (target_config.type != tt::DEVICE::Invalid) {
        log_assert(target_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected target backend to be closed successfully");
    }


    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::time_t end_time = std::chrono::system_clock::to_time_t(end);
    log_info(tt::LogTest, "{} - Finished test for netlist {}. Duration was {}s", strtok(std::ctime(&end_time), "\n"), args.netlist_path, elapsed_seconds.count());

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
    return 0;
}

int main(int argc, char** argv) {
    int return_code = 0;

    std::vector<std::string> input_args(argv, argv + argc);
    return_code = run(input_args);
    return return_code;
}