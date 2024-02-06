// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>

#include "common/buda_soc_descriptor.h"
#include "common/model/test_common.hpp"
#include "tt_golden.hpp"
#include "verif.hpp"
#include "verif_comparison_types.hpp"
#include "verif/verif_utils.hpp"

using namespace tt;
using namespace tt::golden;
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;
/*! test_golden_to_model
    general tool to run any given netlist through to model
*/
int main(int argc, char** argv) {
    std::vector<std::string> verif_args(argv, argv + argc);
    std::string version_string = "v0.0";
    log_info(tt::LogTest, "test_golden_to_model {}", version_string);
    std::string netlist_path;
    int num_loops = 1;
    bool help = false;
    bool parse_only = false;
    int seed = -1;
    string arch_name;
    string cluster_description_path;
    string help_string;
    help_string += "test_golden_to_model --netlist [netlist_path] --pytorch-bin [path to bin files] --num-loops [1]\n";
    help_string += "--num-loops  <>             : Number of loops of input_count to run (Default: 1)\n";
    help_string += "--seed <>                   : Number of inputs (Default: random seed)\n";
    help_string += "--netlist <>                : Path to netlist file\n";
    help_string +=
        "--parse-only                : Only parses the netlist and doesn't run golden or model.  (Default: false)\n";
    help_string +=
        "--arch                      : specify the device architecture. Options: \"grayskull\", \"wormhole\", "
        "\"wormhole_a0\" (Default: grayskull)\n";
    help_string +=
        "--cluster-description       : path to a cluster description file of the target machine. Defaults to single "
        "chip version of chosen arch";
    help_string += "--help                      : Prints this message\n";
    try {
        std::tie(num_loops, verif_args) =
            verif_args::get_command_option_int32_and_remaining_args(verif_args, "--num-loops", 1);
        std::tie(seed, verif_args) = verif_args::get_command_option_int32_and_remaining_args(verif_args, "--seed", -1);
        std::tie(netlist_path, verif_args) = verif_args::get_command_option_and_remaining_args(verif_args, "--netlist");
        std::tie(parse_only, verif_args) =
            verif_args::has_command_option_and_remaining_args(verif_args, "--parse-only");
        std::tie(arch_name, verif_args) =
             verif_args::get_command_option_and_remaining_args(verif_args, "--arch", std::getenv("ARCH_NAME"));
        std::tie(cluster_description_path, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--cluster-description", "");
        std::tie(help, verif_args) = verif_args::has_command_option_and_remaining_args(verif_args, "--help");
        verif_args::validate_remaining_args(verif_args);
    } catch (const std::exception& e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (help) {
        log_info(tt::LogNetlist, "Usage Help:\n{}", help_string);
        exit(0);
    }
    // Setup
    if (seed == -1) {
        tt_rnd_set_seed(tt_gen_seed());
    } else {
        tt_rnd_set_seed(seed);
    }
    tt_golden golden(netlist_path);
    if (parse_only) {
        log_info(tt::LogNetlist, "Parsing Successful", help_string);
        exit(0);
    }

    golden.initialize();
    // model.initialize();
    std::map<string, tt_queue_info> input_queue_info_map = golden.get_host_queue_info_map();
    std::uint32_t num_queues = input_queue_info_map.size();

    bool pass = true;

    VerifTestConfig test_config = verif::test_config::read_from_yaml_file(netlist_path);

    std::map<string, std::vector<tt_tensor>> input_queues;
    log_info(tt::LogTest, "Running Test num_loops={}", num_loops);
    for (int n = 0; n < num_loops; ++n) {
        golden.reset_queues();
        // model.reset_queues();
        //  Process input queues -- Process a microbatch worth of inputs for all queues in graph.
        for (const auto& it : input_queue_info_map) {
            string q_name = it.first;
            tt_queue_info q_info = it.second;
            tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
            const VerifStimulusConfig &stimulus_config = verif::stimulus::get_config(q_name, test_config.stimulus_configs);
            input_queues.insert({q_name, std::vector<tt_tensor>(q_info.input_count)});
            for (int b = 0; b < q_info.input_count; ++b) {
                input_queues.at(q_name).at(b) = tt_tensor(md);
                generate_tensor(stimulus_config, input_queues.at(q_name).at(b));
                verif::push_tensor(golden, q_name, &input_queues.at(q_name).at(b));
            }
        }

        // Run program -- Need to make sure program is only 1 microbatch programed as looping is handled by test.
        golden.run_programs();
        // model.run_programs();

#if 0
        // Checking for q_info.input_count
        for (const auto& it : golden.get_device_queue_info_map()) {
            string q_name = it.first;
            tt_queue_info q_info = it.second;
            for (int b = 0; b < q_info.input_count; ++b) {
                tt_tensor golden_output = *golden.pop_output(q_info);
                auto model_output = model.get_output(q_info, b);
                log_info(tt::LogTest, "Checking Entry idx={} in Loop idx={}", b, n);
                if (not compare_tensors(
                        golden_output,
                        *model_output,
                        VerifComparisonConfig(ComparisonType::AllCloseHw, ComparisonVerbosity::Concise))) {
                    pass = false;
                    log_error("Entry idx={} in Loop idx={} Mismatched", b, n);
                }
            }
        }
#endif
    }

    for (const auto& it : golden.get_device_queue_info_map()) {
        tt_queue_info q_info = it.second;
        VerifComparisonConfig config = verif::comparison::get_config(q_info.name, test_config.comparison_configs);
        bool verbose = config.verbosity == ComparisonVerbosity::Verbose;
        if (verbose) {
            tt_tensor golden_output;
            for (int b = 0; b < q_info.input_count * num_loops; ++b) {
                if (q_info.type == IO_TYPE::Queue) {
                    golden_output = *golden.pop_output(q_info);
                } else {
                    if (b > 0) {
                        continue;
                    }
                    golden_output = *golden.get_output(q_info, 0);
                }
            }

            int w = 0;
            for (const auto& wv : golden_output.tile_tensor) {
                int z = 0;
                for (const auto& zv : wv) {
                    int y = 0;
                    for (const auto& yv : zv) {
                        int x = 0;
                        for (const auto& t : yv) {
                            log_info(tt::LogTest, "Golden Output [{}][{}][{}][{}]\n{}", w, z, y, x, t.get_string());
                            x++;
                        }
                        y++;
                    }
                    z++;
                }
                w++;
            }
        }
    }

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
}
