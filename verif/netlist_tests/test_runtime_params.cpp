// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>

#include "tt_backend.hpp"
#include "runtime.hpp"
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
            input_args, "--netlist", "verif/netlist_tests/netlists/basic_program/netlist_runtime_params.yaml");
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

/*! Base netlist setup.  This only runs 1 loop and super basic, but all the infrastructure pieces are in this to be based
 *  off of
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
        backend_config = {.type=backend_to_test}; 
    } else if (backend_to_test == tt::DEVICE::Model) {
        backend_config = {
            .type = backend_to_test,
            .ignore_data_format_precision = true,
        };
    } else if ((backend_to_test == tt::DEVICE::Silicon) or (backend_to_test == tt::DEVICE::Versim)) {
        backend_config = {
            .type = backend_to_test, 
            .arch = (args.arch_name == "grayskull")? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE, 
            .optimization_level=0, 
            .output_dir=args.output_dir,
        }; 
    } else {
        log_fatal ("{} is not a supported backend to compile to for this test", args.backend);
    }
    // Compare to Config
    tt_backend_config compare_to_config;
    tt::DEVICE compare_to = get_device_from_string(args.compare_to);
    if (compare_to == tt::DEVICE::Golden) {
        compare_to_config = {.type=compare_to}; 
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

    // TEST START, do custom stuff if you want
    for (int n = 0; n < args.num_loops; ++n) {
        // DRIVE INPUTS -- Driven however test wants to set it up
        std::vector input_names = {"q0"};  // INPUTS:: FILL-ME
        std::unordered_map<std::string, std::vector<tt_tensor>> inputs(input_names.size());
        for (const auto &input_name : input_names) {
            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(input_name);
            tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc); 
            inputs.insert({input_name, std::vector<tt_tensor>(q_desc.input_count)});
            for (int b = 0; b < q_desc.input_count; ++b) {
                inputs.at(input_name).at(b) = tt_tensor(md);
                generate_tensor(verif::stimulus::get_config(input_name, stimulus_configs), inputs.at(input_name).at(b));
                verif::push_tensor(*target_backend, input_name, &inputs.at(input_name).at(b));
                if (compare_to_backend) {
                    verif::push_tensor(*compare_to_backend, input_name, &inputs.at(input_name).at(b));
                }
            }
        }
        // RUN_PROGRAM
        std::map <std::string, std::string> program_parameters = {{"$num_loops", "1"}, {"$rd_ptr", "0"}};
        target_backend->run_program("working_program", program_parameters);    

        // GENERATE EXPECTED OR RUN_PROGRAM FOR COMPARE_TO 
        std::vector output_names = {"output"}; // OUTPUTS:: FILL-ME
        std::unordered_map<std::string, std::vector<tt_tensor>> expected(output_names.size());
        tt_dram_io_desc q_desc = target_backend->get_queue_descriptor("output");
        expected.insert({"output", std::vector<tt_tensor>(q_desc.input_count)});

        if (compare_to_backend) {
            compare_to_backend->run_program("working_program", program_parameters);  
        } else {
            for (int b = 0; b < q_desc.input_count; ++b) {
                // Expected Calculation
                expected.at("output").at(b) = inputs.at("q0").at(b);
            }
        }

        // READ OUTPUTS 
        for (const auto &output_name : output_names) {
            log_info(tt::LogTest, "Reading and Checking Output Queues");
            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
            tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
            // Checking for q_info.input_count
            for (int b = 0; b < q_desc.input_count; ++b) {
                tt_tensor output(md);
                verif::get_and_pop_tensor(*target_backend, output_name, &output); 
                if (compare_to_backend) {
                    expected.at(output_name).at(b) = tt_tensor(md);
                    verif::get_and_pop_tensor(*compare_to_backend, output_name, &expected.at(output_name).at(b)); 
                }
                log_info(tt::LogTest, "Checking Entry idx={} for output={}", b, output_name);
                if (not compare_tensors(
                        expected.at(output_name).at(b),
                        output,
                        verif::comparison::get_config(output_name, comparison_configs))) {
                    pass = false;
                    log_error("Entry idx={} for output={} Mismatched", b, output_name);
                }
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
