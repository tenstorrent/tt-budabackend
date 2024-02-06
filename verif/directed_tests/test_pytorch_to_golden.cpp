// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <fstream>

#include "tt_golden.hpp"
#include "verif.hpp"

using namespace tt;
using namespace tt::golden;
using namespace verif::comparison;
/*! test_pytorch_to_golden
    tool that is used to validate front end generated netlists
*/
int main(int argc, char **argv) {
    std::vector<std::string> verif_args(argv, argv + argc);
    std::string version_string = "v0.0";
    log_info(tt::LogTest, "test_pytorch_to_golden {}", version_string);
    std::string netlist_path;
    std::string pytorch_bin_path;
    int num_loops = 1;
    string verbosity = "";
    bool help = false;
    bool parse_only = false;
    bool skip_pybuda_api = false;
    string help_string;
    help_string +=
        "test_pytorch_to_golden --netlist [netlist_path] --pytorch-bin [path to bin files] --num-loops [1]\n";
    help_string += "--num-loops  <>  : Number of loops of input_count to run (Default: 1)\n";
    help_string += "--verbosity  <>  : verbosity for checking (Default: concise)\n";
    help_string += "--pytorch-bin <> : Path to the pytorch binary files to corresponding to netlist\n";
    help_string += "--netlist <>     : Path to netlist file\n";
    help_string += "--parse-only     : Only parses the netlist and doesn't run golden.  (Default: false)\n";
    help_string += "--skip-pybuda-api: Push inputs using non pybuda api -- debugging tool to ensure pybuda api bugs (Default: false)\n";
    help_string += "--help           : Prints this message\n";
    try {
        std::tie(num_loops, verif_args) =
            verif_args::get_command_option_int32_and_remaining_args(verif_args, "--num-loops", 1);
        std::tie(pytorch_bin_path, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--pytorch-bin");
        std::tie(verbosity, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--verbosity", "concise");
        std::tie(netlist_path, verif_args) = verif_args::get_command_option_and_remaining_args(verif_args, "--netlist");
        std::tie(parse_only, verif_args) =
            verif_args::has_command_option_and_remaining_args(verif_args, "--parse-only");
        std::tie(skip_pybuda_api, verif_args) =
            verif_args::has_command_option_and_remaining_args(verif_args, "--skip-pybuda-api");
        std::tie(help, verif_args) = verif_args::has_command_option_and_remaining_args(verif_args, "--help");
        verif_args::validate_remaining_args(verif_args);
    } catch (const std::exception &e) {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", help_string);
        exit(1);
    }
    if (help) {
        log_info(tt::LogNetlist, "Usage Help:\n{}", help_string);
        exit(0);
    }
    tt_golden golden(netlist_path);

    if (parse_only) {
        log_info(tt::LogNetlist, "Parsing Successful", help_string);
        exit(0);
    }
    string saved_pytorch_bin_root = pytorch_bin_path + "/";
    bool init_rams = true;
    std::map<string, tt_queue_info> input_queue_info_map = golden.get_host_queue_info_map(init_rams);
    std::uint32_t num_queues = input_queue_info_map.size();

    bool pass = true;
    std::map<string, std::vector<tt_tensor>> input_queues;
    log_info(
        tt::LogTest,
        "Running Test num_loops={}",
        num_loops);
    for (int n = 0; n < num_loops; ++n) {
        // Process input queues -- Process a microbatch worth of inputs for all queues in graph.
        for (const auto &it : input_queue_info_map) {
            string q_name = it.first;
            tt_queue_info q_info = it.second;
            tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
            input_queues.insert({q_name, std::vector<tt_tensor>(q_info.input_count)});
            for (int b = 0; b < q_info.input_count; ++b) {
                input_queues.at(q_name).at(b) = tt_tensor(md);
                if (!tt::data_binary::does_file_exists(saved_pytorch_bin_root, q_name, q_info.input_count * n + b))  {
                    verif::stimulus::generate_tensor(
                        VerifStimulusConfig ({ .type=StimulusType::Uniform, .uniform_lower_bound=-1.0f, .uniform_upper_bound=1.0f}),
                        input_queues.at(q_name).at(b));
                    verif::write_tensor_to_binary(saved_pytorch_bin_root, q_name, q_info.input_count * n + b, input_queues.at(q_name).at(b));
                } else {
                    verif::read_tensor_from_binary(
                        saved_pytorch_bin_root, q_info, q_info.input_count * n + b, input_queues.at(q_name).at(b), init_rams);
                }
                if (skip_pybuda_api) {
                    golden.push_input(q_info, std::make_shared<tt_tensor>(input_queues.at(q_name).at(b)), b);
                } else {
                    verif::push_tensor(golden, q_info.name, &input_queues.at(q_name).at(b), b);
                }
            }
        }

        // Run program -- Need to make sure program is only 1 microbatch programed as looping is handled by test.
        golden.run_programs();

        VerifComparisonConfig cconfig(ComparisonType::AllCloseHw, verif::comparison::get_comparison_verbosity(verbosity));
        cconfig.atol = 0.05;
        cconfig.rtol = 0.15; 
        cconfig.check_pct = 0.99;
        for (const auto &it : golden.get_device_queue_info_map()) {
            string q_name = it.first;
            log_info(tt::LogTest, "Reading and Checking Output Queue={}", q_name);
            if (q_name.find("e2e") != std::string::npos) {
                log_warning(tt::LogTest, "Skipping Checking of Epoch2Epoch Queues {}", q_name);
                continue;  // FIXME: Ignore epoch2epoch connections for now
            } else if (q_name.find("grad_acc_mha.bert.encoder.layer.0.attention.self.key.bias") != std::string::npos) {
                log_warning(tt::LogTest, "Skipping Checking of Queues {}", q_name);
                continue;  // FIXME: Ignore epoch2epoch connections for now
            } else if (q_name.find("grad_acc_ff.bert.encoder.layer.0.attention.self.key.bias") != std::string::npos) {
                log_warning(tt::LogTest, "Skipping Checking of Queues {}", q_name);
                continue;  // FIXME: Ignore epoch2epoch connections for now
            }
            
            tt_queue_info q_info = it.second;
            tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
            for (int b = 0; b < q_info.input_count; ++b) {
                tt_tensor golden_output(md);
                if (skip_pybuda_api) {
                    golden_output = *golden.get_output(q_info, b);
                    if (q_info.type != IO_TYPE::RandomAccess) {
                        *golden.pop_output(q_info);
                    }
                } else {
                    verif::get_and_pop_tensor(golden, q_info.name, &golden_output, b);
                }
                tt_tensor expected(md);
                if (!tt::data_binary::does_file_exists(saved_pytorch_bin_root, q_name, q_info.input_count * n + b))  {
                    verif::write_tensor_to_binary(saved_pytorch_bin_root, q_name, q_info.input_count * n + b, golden_output);
                }
                verif::read_tensor_from_binary(saved_pytorch_bin_root, q_info, q_info.input_count * n + b, expected, false);
                log_info(tt::LogTest, "Checking Entry idx={} in Loop idx={}", b, n);
                if (not compare_tensors(
                        expected,
                        golden_output,
                        cconfig)) {
                    pass = false;
                    log_error("Entry idx={} in Loop idx={} Mismatched", b, n);
                }
            }
        }
    }
    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
}
