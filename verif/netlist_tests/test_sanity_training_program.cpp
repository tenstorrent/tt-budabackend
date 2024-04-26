// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>

#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"

using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

struct test_args {
    std::string netlist_path = "";
    std::string output_dir = "";
    int num_loops = 1;
    std::string backend = "";
    std::string compare_to = "";
    bool force_sw_tilize = false;
    std::string arch_name = "";
    std::string vcd_dump_cores = "";
    int seed = -1;
    bool help = false;
};

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--num-loops <>       : Number of loops of input_counts to run (Default: 1)\n";
    help_string += "--seed <>            : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>         : Path to netlist file\n";
    help_string += "--backend            : Backend to run to (Default: Golden)\n";
    help_string += "--compare-to         : Backend to compare to (Default: None -- Custom expected)\n";
    help_string += "--force-sw-tilize    : Force SW tilize for tt_runtime (default will use HW-Tilize when possible)\n";
    help_string +=
        "--arch <>            : specify the device architecture. Options: \"grayskull\", \"wormhole\", \"wormhole_b0\" "
        "(Default: grayskull)\n";
    help_string += "--vcd-dump-cores <>  : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\" ";
    help_string += "--help               : Prints this message\n";
    try {
        std::tie(args.output_dir, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--outdir", "");
        if (args.output_dir == "") {
            args.output_dir = verif_filesystem::get_output_dir(__FILE__, input_args);
        }
        std::tie(args.num_loops, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--num-loops", 1);
        std::tie(args.seed, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--seed", -1);
        std::tie(args.netlist_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, "--netlist", "verif/netlist_tests/netlists/basic_program/netlist_sanity_training_program.yaml");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.backend, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--backend", "Golden");
        std::tie(args.compare_to, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--compare-to", "Invalid");
        std::tie(args.force_sw_tilize, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--force-sw-tilize");
        std::tie(args.arch_name, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--arch", "grayskull");
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        verif_args::validate_remaining_args(input_args);
    } catch (const std::exception& e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (args.help) {
        log_info(tt::LogNetlist, "Usage Help:\n{}", help_string);
        exit(0);
    }
    return args;
}

/*! Base netlist setup.  This only runs 1 loop and super basic, but all the infrastructure pieces are in this to be
 * based off of
 */
int main(int argc, char** argv) {
    // Get Test parameters
    std::vector<std::string> input_args(argv, argv + argc);
    log_info(tt::LogTest, "test_op");
    test_args args = parse_test_args(input_args);
    // Seeding of test for randomization
    if (args.seed == -1) {
        tt_rnd_set_seed(tt_gen_seed());
    } else {
        tt_rnd_set_seed(args.seed);
    }
    // Backend Configuration
    tt_backend_config backend_config;
    tt::DEVICE backend_to_test = get_device_from_string(args.backend);
    if (backend_to_test == tt::DEVICE::Golden) {
        backend_config = {.type = backend_to_test};
    } else if (backend_to_test == tt::DEVICE::Model) {
        backend_config = {
            .type = backend_to_test,
            .ignore_data_format_precision = true,
        };
    } else if ((backend_to_test == tt::DEVICE::Silicon) or (backend_to_test == tt::DEVICE::Versim)) {
        backend_config = {
            .type = backend_to_test,
            .arch = (args.arch_name == "grayskull") ? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE,
            .optimization_level = 0,
            .output_dir = args.output_dir,
        };
    } else {
        log_fatal("{} is not a supported backend to compile to for this test", args.backend);
    }
    // Compare to Config
    tt_backend_config compare_to_config;
    tt::DEVICE compare_to = get_device_from_string(args.compare_to);
    if (compare_to == tt::DEVICE::Golden) {
        compare_to_config = {.type = compare_to};
    }

    auto config = verif::test_config::read_from_yaml_file(args.netlist_path, "verif/netlist_tests/default_test_config.yaml");
    // Get Stimulus Config
    auto& stimulus_configs = config.stimulus_configs;
    // Get Comparison Config
    auto comparison_configs = config.comparison_configs;

    bool pass = true;

    // INITIALIZATION OF BACKENDS
    std::shared_ptr<tt_backend> target_backend = tt_backend::create(args.netlist_path, backend_config);
    target_backend->initialize();
    std::shared_ptr<tt_backend> compare_to_backend;
    if (compare_to == tt::DEVICE::Golden) {
        compare_to_backend = tt_backend::create(args.netlist_path, compare_to_config);
        compare_to_backend->initialize();
    }

    // Initialize Weights in the beginning.
    std::unordered_map<std::string, std::vector<tt_tensor>> inputs;
    std::unordered_map<std::string, std::vector<tt_tensor>> expected;
    std::string weight_name = "weights";  // INPUTS:: FILL-ME
    tt_dram_io_desc weight_q_desc = target_backend->get_queue_descriptor(weight_name);
    tt_tensor_metadata weight_md = get_tensor_metadata_for_queue(weight_q_desc);
    inputs.insert({weight_name, std::vector<tt_tensor>(weight_q_desc.input_count)});
    expected.insert({weight_name, std::vector<tt_tensor>(weight_q_desc.input_count)});
    for (int b = 0; b < weight_q_desc.input_count; ++b) {
        inputs.at(weight_name).at(b) = tt_tensor(weight_md);
        generate_tensor(verif::stimulus::get_config(weight_name, stimulus_configs), inputs.at(weight_name).at(b));
        verif::push_tensor(*target_backend, weight_name, &inputs.at(weight_name).at(b), b);
        if (compare_to_backend) {
            verif::push_tensor(*compare_to_backend, weight_name, &inputs.at(weight_name).at(b), b);
        }
        expected.at(weight_name).at(b) = inputs.at(weight_name).at(b);
    }

    tt_dram_io_desc q_desc = target_backend->get_queue_descriptor("q0");
    int num_accumulation_steps = 3;
    int num_pipeline_stages = q_desc.bufq_num_slots / q_desc.input_count;
    for (int n = 0; n < args.num_loops; ++n) {
        std::map<std::string, std::string> program_parameters = {};
        bool zero_accumulated_gradients = true;

        // RUN ACCUMULATION STEP
        for (int n_as = 0; n_as < num_accumulation_steps; n_as++) {
            string input_name = "q0";                       // INPUTS:: FILL-ME
            std::string fwd_output_name = "fwd_outputs.0";  // OUTPUTS:: FILL-ME
            tt_dram_io_desc input_q_desc = target_backend->get_queue_descriptor(input_name);
            tt_dram_io_desc fwd_output_q_desc = target_backend->get_queue_descriptor(fwd_output_name);
            std::queue<tt_tensor> saved_inputs = {};
            std::queue<tt_tensor> saved_fwd_outputs = {};

            // RUN FORWARD STEP
            for (int n_pipeline = 0; n_pipeline < num_pipeline_stages; n_pipeline++) {
                tt_tensor_metadata md = get_tensor_metadata_for_queue(input_q_desc);
                inputs.insert({input_name, std::vector<tt_tensor>(input_q_desc.input_count)});
                for (int b = 0; b < input_q_desc.input_count; ++b) {
                    inputs.at(input_name).at(b) = tt_tensor(md);
                    generate_tensor(
                        verif::stimulus::get_config(input_name, stimulus_configs), inputs.at(input_name).at(b));
                    verif::push_tensor(*target_backend, input_name, &inputs.at(input_name).at(b));
                    if (compare_to_backend) {
                        verif::push_tensor(*compare_to_backend, input_name, &inputs.at(input_name).at(b));
                    }
                    saved_inputs.push(inputs.at(input_name).at(b));
                }

                // Run Forward
                program_parameters = {};
                target_backend->run_program("fwd", program_parameters);

                expected.insert({fwd_output_name, std::vector<tt_tensor>(fwd_output_q_desc.input_count)});
                if (compare_to_backend) {
                    compare_to_backend->run_program("fwd", program_parameters);
                } else {
                    for (int b = 0; b < fwd_output_q_desc.input_count; ++b) {
                        // Expected Calculation
                        expected.at(fwd_output_name).at(b) = inputs.at("q0").at(b).add(inputs.at("weights").at(0));
                        saved_fwd_outputs.push(expected.at(fwd_output_name).at(b));
                    }
                }
                log_info(tt::LogTest, "Reading and Checking Queue={}", fwd_output_name);
                tt_tensor_metadata fwd_output_md = get_tensor_metadata_for_queue(fwd_output_q_desc);
                // Checking for q_info.input_count
                for (int b = 0; b < fwd_output_q_desc.input_count; ++b) {
                    tt_tensor output(fwd_output_md);
                    verif::get_and_pop_tensor(*target_backend, fwd_output_name, &output);
                    if (compare_to_backend) {
                        expected.at(fwd_output_name).at(b) = tt_tensor(fwd_output_md);
                        verif::get_and_pop_tensor(
                            *compare_to_backend, fwd_output_name, &expected.at(fwd_output_name).at(b));
                    }
                    log_info(tt::LogTest, "Checking Entry idx={} for output={}", b, fwd_output_name);
                    if (not compare_tensors(
                            expected.at(fwd_output_name).at(b),
                            output,
                            verif::comparison::get_config(fwd_output_name, comparison_configs))) {
                        pass = false;
                        log_error("Entry idx={} for output={} Mismatched", b, fwd_output_name);
                    }
                }
            }

            log_assert(
                (saved_inputs.size() == input_q_desc.input_count * num_pipeline_stages) and
                    (saved_fwd_outputs.size() == fwd_output_q_desc.input_count * num_pipeline_stages),
                "Must have enough fwd inputs={} and outputs={} generated by fwd program = "
                "input_count({})*num_pipeline_stages({})",
                saved_inputs.size(),
                saved_fwd_outputs.size(),
                input_q_desc.input_count,
                num_pipeline_stages);

            // RUN FORWARD STEP
            std::string bwd_output_name = {"acc.gradients"};  // OUTPUTS:: FILL-ME
            tt_dram_io_desc bwd_output_q_desc = target_backend->get_queue_descriptor(bwd_output_name);
            tt_tensor_metadata bwd_output_md = get_tensor_metadata_for_queue(bwd_output_q_desc);
            for (int n_pipeline = 0; n_pipeline < num_pipeline_stages; n_pipeline++) {
                program_parameters = {{"$zero_accumulated_gradients", to_string(zero_accumulated_gradients)}};
                target_backend->run_program("bwd", program_parameters);

                expected.insert({bwd_output_name, std::vector<tt_tensor>(bwd_output_q_desc.input_count)});
                if (compare_to_backend) {
                    compare_to_backend->run_program("bwd", program_parameters);
                } else {
                    if (zero_accumulated_gradients) {
                        expected.at(bwd_output_name).at(0) = tt_tensor(bwd_output_md);
                        generate_tensor(
                            {.type = StimulusType::Constant, .constant_value = 0.0f},
                            expected.at(bwd_output_name).at(0));  // Zero out parameters
                        zero_accumulated_gradients = false;
                    }
                    for (int b = 0; b < fwd_output_q_desc.input_count; ++b) {
                        // Expected Calculation
                        auto matmul_output = saved_inputs.front().matmul(saved_fwd_outputs.front());
                        saved_inputs.pop();
                        saved_fwd_outputs.pop();
                        expected.at(bwd_output_name).at(0) = expected.at(bwd_output_name).at(0).add(matmul_output);
                    }
                }

                log_info(tt::LogTest, "Reading and Checking Queue={}", bwd_output_name);
                // Checking for q_info.input_count
                for (int b = 0; b < bwd_output_q_desc.input_count; ++b) {
                    tt_tensor output(bwd_output_md);
                    verif::get_and_pop_tensor(*target_backend, bwd_output_name, &output, b);
                    if (compare_to_backend) {
                        expected.at(bwd_output_name).at(b) = tt_tensor(bwd_output_md);
                        verif::get_and_pop_tensor(
                            *compare_to_backend, bwd_output_name, &expected.at(bwd_output_name).at(b), b);
                    }
                    log_info(tt::LogTest, "Checking Entry idx={} for output={}", b, bwd_output_name);
                    if (not compare_tensors(
                            expected.at(bwd_output_name).at(b),
                            output,
                            verif::comparison::get_config(bwd_output_name, comparison_configs))) {
                        pass = false;
                        log_error("Entry idx={} for output={} Mismatched", b, bwd_output_name);
                    }
                }
            }

            log_assert(
                (saved_inputs.size() == 0) and (saved_fwd_outputs.size() == 0),
                "All fwd inputs={} and outputs={} must be consumed by bwd program",
                saved_inputs.size(),
                saved_fwd_outputs.size());
        }

        // RUN OPTIMIZER
        target_backend->run_program("opt", program_parameters);
        if (compare_to_backend) {
            compare_to_backend->run_program("opt", program_parameters);
        } else {
            expected.at(weight_name).at(0) =
                expected.at("acc.gradients").at(0).add(inputs.at(weight_name).at(0)).sigmoid();
            inputs.at(weight_name).at(0) = expected.at(weight_name).at(0);
        }

        log_info(tt::LogTest, "Reading and Checking Queue={}", weight_name);
        // Checking for q_info.input_count
        for (int b = 0; b < weight_q_desc.input_count; ++b) {
            tt_tensor output(weight_md);
            verif::get_and_pop_tensor(*target_backend, weight_name, &output, b);
            if (compare_to_backend) {
                expected.at(weight_name).at(b) = tt_tensor(weight_md);
                verif::get_and_pop_tensor(*compare_to_backend, weight_name, &expected.at(weight_name).at(b), b);
            }
            log_info(tt::LogTest, "Checking Entry idx={} for output={}", b, weight_name);
            if (not compare_tensors(
                    expected.at(weight_name).at(b),
                    output,
                    verif::comparison::get_config(weight_name, comparison_configs))) {
                pass = false;
                log_error("Entry idx={} for output={} Mismatched", b, weight_name);
            }
        }
    }
    target_backend->finish();
    if (compare_to_backend) {
        compare_to_backend->finish();
    }
    // TEST END, do custom stuff if you want
    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
}
