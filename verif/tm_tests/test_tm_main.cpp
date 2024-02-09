// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <chrono>
#include <fstream>
#include <filesystem>

#include "tt_backend.hpp"
#include "runtime.hpp"
#include "tt_golden.hpp"
#include "verif.hpp"

using namespace tt;
using namespace tt::golden;
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif;

int main(int argc, char** argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_tm_main since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    // Args + test infra
    std::vector<std::string> verif_args(argv, argv + argc);
    log_info(tt::LogTest, "test_tm");
    std::string netlist_path;
    perf::PerfDesc perf_desc;
    std::string output_dir;
    std::string zero_queue;
    int num_loops = 1;
    bool help = false;
    bool run_silicon = false;
    bool force_sw_tilize = false;
    bool randomize = false;
    bool dont_compare = false;
    bool compare_with_saved_golden = false;
    bool determinism_tests = false;
    bool exit_at_failure = false;
    int seed = -1;
    std::string vcd_dump_cores = "";
    std::vector<int> input_per_sec_thresh;
    string input_per_sec_thresh_arg;
    string comp_verbo = "AllFails";
    std::string arch_name;
    std::string cluster_desc;
    string help_string;
    help_string += "test_tm --netlist [netlist_path] --num-inputs [1]\n";
    help_string += "--num-loops <>       : Number of loops of input_counts to run (Default: 1)\n";
    help_string += "--seed <>          : Number of inputs (Default: random seed)\n";
    help_string += "--netlist <>       : Path to netlist file\n";
    help_string += "--dump-perf-events              : Enables the performance monitoring\n";
    help_string += "--dump-perf-events-intermediate : Enables the performance monitoring with intermediate dumps to DRAM\n";
    help_string += "--perf-level                    : Determines the granularity of the information that will be recorded during runtime. Options: 0 or 1\n";
    help_string += "--perf-op-mode                  : Any op in the graph can be configured in different decoupling modes.\n";
    help_string += "--input_per_sec_thresh          : Inputs per second threshold. Test will fail if device reports under this number. Multiple graphs can be specified by providing multiple numbers spaced with _. e.g. 50_51_52 for 3 graphs.\n";
    help_string += "--target-aiclk                  : Will force the clock frequency in MHz. e.g.: --target-aiclk 1200\n";
    help_string += "--comp-verbo <>    : Concise: only first tile failure per entry -- AllFails: All failures, Verbose: All Passes and Failure Diffs are printed (Default: AllFails)\n";
    help_string += "--silicon          : Run on Silicon (Default: Versim)\n";
    help_string += "--zero-queue       : which op to bypass (none, input0, input1, input2) - Overrides randomize flag\n";
    help_string += "--randomize        : completely randomized input data (Default: false)\n";
    help_string += "--force-sw-tilize  : Force SW tilize for tt_runtime (default will use HW-Tilize when possible)\n";
    help_string += "--skip-hw-untilize : Skip HW untilize for tt_runtime\n";
    help_string += "--dont_compare     : Don't compare expected results with actual results\n";    
    help_string += "--compare-with-saved-golden : Compare against golden file saved to disk, or save to disk if one doesnt exist.\n";
    help_string += "--arch <>          : specify the device architecture. Options: \"grayskull\", \"wormhole\", \"wormhole_b0\" (Default: grayskull)\n";
    help_string += "--vcd-dump-cores <> : : specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\" \n";
    help_string += "--help             : Prints this message\n";
    try {
        std::tie(output_dir, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--outdir", "");
        // Setup
        if (output_dir == "") {
            output_dir = verif_filesystem::get_output_dir(__FILE__, verif_args);
        }
        std::tie(num_loops, verif_args) =
            verif_args::get_command_option_int32_and_remaining_args(verif_args, "--num-loops", 1);
        std::tie(seed, verif_args) = verif_args::get_command_option_uint32_and_remaining_args(verif_args, "--seed", -1);
        std::tie(comp_verbo, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--comp-verbo", "AllFails");
        std::tie(netlist_path, verif_args) = verif_args::get_command_option_and_remaining_args(
            verif_args, "--netlist", "verif/tm_tests/netlist_tm_tests_base.yaml");
        perf_desc = perf::PerfDesc(verif_args, netlist_path);
        std::tie(input_per_sec_thresh_arg, verif_args) = verif_args::get_command_option_and_remaining_args(verif_args, "--input_per_sec_thresh", "");
        std::tie(zero_queue, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--zero-queue", "none");
        std::tie(run_silicon, verif_args) = verif_args::has_command_option_and_remaining_args(verif_args, "--silicon");
        std::tie(force_sw_tilize, verif_args) =
            verif_args::has_command_option_and_remaining_args(verif_args, "--force-sw-tilize");
        std::tie(dont_compare, verif_args) =
            verif_args::has_command_option_and_remaining_args(verif_args, "--dont_compare");
        std::tie(compare_with_saved_golden, verif_args) =
            verif_args::has_command_option_and_remaining_args(verif_args, "--compare-with-saved-golden");
        std::tie(randomize, verif_args) = verif_args::has_command_option_and_remaining_args(verif_args, "--randomize");
        std::tie(arch_name, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--arch", "grayskull");
        std::tie(determinism_tests, verif_args) = 
            verif_args::has_command_option_and_remaining_args(verif_args, "--determinism-tests");
        std::tie(exit_at_failure, verif_args) = 
            verif_args::has_command_option_and_remaining_args(verif_args, "--exit-at-failure");
        std::tie(vcd_dump_cores, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--vcd-dump-cores", "");
        std::tie(cluster_desc, verif_args) =
            verif_args::get_command_option_and_remaining_args(verif_args, "--cluster-desc", "");
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

    bool pass = true;
    // Setup
    if (seed == -1) {
        random::tt_rnd_set_seed(random::tt_gen_seed());
    } else {
        random::tt_rnd_set_seed(seed);
    }

    bool golden_dir_exists = false;
    std::string golden_dir_path;
    std::string stim_bin_path;
    if (compare_with_saved_golden) {
        int ext_indx = netlist_path.size()-1;
        while (ext_indx > 0 && netlist_path[ext_indx] != '.') {
            ext_indx--;
        }
        if (ext_indx == 0)
            log_fatal("Invalid netlist path");
        stim_bin_path = netlist_path.substr(0, ext_indx) + "_stim_" + std::to_string(seed);
        golden_dir_path = netlist_path.substr(0, ext_indx) + "_golden_" + std::to_string(seed);
        std::filesystem::path golden_dir{golden_dir_path.c_str()};
        golden_dir_exists = std::filesystem::exists(golden_dir);
    }

    // Runtime setup
    bool init_rams = true;
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, 0, run_silicon);
    config.device_params = tt_device_params{};
    config.cluster_descriptor_path = cluster_desc;
    //config.device_params.vcd_dump_cores = {"1-1", "3-1", "5-1", "7-1", "9-1"};
    if (vcd_dump_cores != "") {
        config.device_params.vcd_dump_cores = verif_args::get_vcd_dump_cores_from_arg_string(vcd_dump_cores);
    }

    tt_runtime runtime(netlist_path, config);
    // Runtime init
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");
    tt_runtime_workload& workload = *runtime.get_workload();
    std::string program_name = workload.programs.begin()->first;
    log_assert(workload.programs.size() == 1, "workload.programs.size() != 1");
    

    std::map<string, tt_queue_info> input_queue_info_map;
    std::map<string, tt_queue_info> output_queue_info_map;
    
    // Get IO queues for current netlist. Assume that input queues are populated only by Host (device does not write back to local memory for these tests)
    for(auto const&q : workload.queues){
        if(q.second.my_queue_info.input == "HOST")
            input_queue_info_map.insert({q.second.my_queue_info.name, q.second.my_queue_info});
        else
            output_queue_info_map.insert({q.second.my_queue_info.name, q.second.my_queue_info});
    }

    std::uint32_t num_queues = input_queue_info_map.size();

    // Golden Pathway
    tt_golden golden(netlist_path);
    std::map<string, std::vector<tt_tensor>> input_queues;
    
    if(!determinism_tests && !golden_dir_exists)
        golden.reset_queues();

    VerifTestConfig test_config;
    if(determinism_tests) 
        test_config = verif::test_config::read_from_yaml_file(netlist_path, "verif/netlist_tests/default_test_config.yaml");
   
    // Process input queues -- Process a microbatch worth of inputs for all queues in graph.
    if ((zero_queue != "none") and (input_queue_info_map.find(zero_queue) == input_queue_info_map.end())) {
        log_fatal("Cannot find input queue {} to zero out", zero_queue);
    }

    int input_count = 0;
    std::unordered_map<std::string, std::vector<tt_tensor>> init_output_tensors;
    for(int loop = 0; loop < num_loops; loop++){
        log_info(tt::LogTest, "Started loop {} ", loop);

        if((determinism_tests and !loop) or !determinism_tests){
            // Only generate inputs once for determinism tests
            for (const auto& it : input_queue_info_map) {
                string q_name = it.first;
                tt_queue_info q_info = it.second;
                tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
                
                input_queues.insert({q_name, std::vector<tt_tensor>(q_info.entries)});
                
                md.is_tilized = true;
                input_count = q_info.input_count;
                for (int b = 0; b < q_info.entries; ++b) {
                    input_queues.at(q_name).at(b) = tt_tensor(md);
                    if (golden_dir_exists) {
                        std::string stim_yaml_path = stim_bin_path + "/";
                        verif::read_tensor_from_binary(stim_yaml_path, q_info, q_info.input_count * loop + b, input_queues.at(q_name).at(b), init_rams);
                    } else {
                        if (zero_queue == q_name) {
                            generate_tensor(
                                {
                                    .type=StimulusType::Constant, 
                                    .constant_value=0.0f
                                }, 
                                input_queues.at(q_name).at(b));
                        } else {
                            if (randomize) {
                                generate_tensor(
                                    {
                                        .type=StimulusType::Normal, 
                                        .normal_mean=0.0f,
                                        .normal_stddev= 1.0f
                                    }, 
                                    input_queues.at(q_name).at(b)); 
                            } else {
                                generate_tensor(
                                    {
                                        .type=StimulusType::DebugTileId, 
                                        .debug_tile_id_base=0.01f, 
                                        .debug_tile_id_step= 0.01f,
                                    }, 
                                    input_queues.at(q_name).at(b));
                            }
                        }
                        if (compare_with_saved_golden && !golden_dir_exists) {
                            std::string stim_yaml_path = stim_bin_path + "/";
                            std::filesystem::create_directories(stim_bin_path);
                            verif::write_tensor_to_binary(stim_yaml_path, q_name, q_info.input_count * loop + b, input_queues.at(q_name).at(b));
                        }
                    }
                
                    if(!determinism_tests && !golden_dir_exists)
                        // When running determinism tests, inputs dont need to be pushed to golden
                        golden.push_input(q_info, std::make_shared<tt_tensor> (input_queues.at(q_name).at(b)));
                }
            }
        }

        // Run program -- Need to make sure program is only 1 microbatch programed as looping is handled by test.
        if(!determinism_tests && !golden_dir_exists)
            // Only run golden if not in determinism testing mode
            golden.run_programs();

        log_assert(input_count > 0, "input count has to be greated than 0");

        // Host input push
        vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();

        log_info(tt::LogTest, "Pushing all inputs...");
        auto push_start = std::chrono::high_resolution_clock::now();

        for (tt_dram_io_desc &io : input_io_desc) {
            tt_queue_info &queue_info = workload.queues.at(io.queue_name).my_queue_info;
            int num_pushes = std::min(queue_info.entries, input_count);
            constexpr int default_ram_ptr = 0;
            tt::io::push_host_input(
                io,
                [&input_queues](tt_queue_info& queue_info, int entry_idx) {
                    log_assert(
                        entry_idx < input_queues.at(queue_info.name).size(),
                        "Entry accessed by runtime is more than amount of inputs specified in test -- please increase "
                        "inputs to test or decrease entries in queue");  // FIXME Fatal on access issues
                    log_assert(
                        input_queues.find(queue_info.name) != input_queues.end(),
                        "Cannot find queue in inputs created by golden");
                    return input_queues.at(queue_info.name).at(entry_idx);
                },
                num_pushes,
                default_ram_ptr,
                force_sw_tilize
            );
        }

        auto push_end = std::chrono::high_resolution_clock::now();
        auto push_duration = std::chrono::duration_cast<std::chrono::microseconds>(push_end - push_start).count();
        log_info(tt::LogTest, "Push Elapsed Time: {} us", push_duration);

        log_assert(runtime.run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on golden backend");
        log_assert(runtime.wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on golden backend"); // explicitly wfi before popping from device

        // Collect outputs via lazy method that returns all output queues
        log_info(tt::LogTest, "Runtime complete, collecting outputs from device");
        vector<tt_dram_io_desc> output_io_desc = runtime.get_device_output_io_desc();

        log_info(tt::LogTest, "Popping all outputs...");
        auto pop_start = std::chrono::high_resolution_clock::now();
        tt::io::pop_outputs_to_tensor_queues(output_io_desc, 0 /*timeout_in_seconds*/);

        auto pop_end = std::chrono::high_resolution_clock::now();
        auto pop_duration = std::chrono::duration_cast<std::chrono::microseconds>(pop_end - pop_start).count();
        log_info(tt::LogTest, "Pop Elapsed Time: {} us", pop_duration);

        if (!dont_compare) {
            // Checking for q_info.input_count
            VerifComparisonConfig comp(ComparisonType::AllClose, verif::comparison::get_comparison_verbosity(comp_verbo));
            comp.check_pcc = 0.98;
            comp.check_pct = 0.80;
            if (randomize) {
                comp.type = ComparisonType::AllCloseHw;
                comp.atol = 0.01;
                comp.rtol = 0.15;
            } else {
                comp.atol = 0.01;
                comp.rtol = 0.025;
            }
            for (const auto& it : output_queue_info_map) {
                string q_name = it.first;
                tt_queue_info q_info = it.second;
                tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(q_info, false);
                for (int b = 0; b < q_info.input_count; ++b) {
                    //Only pop from golden and compare if not running determinism test
                    std::shared_ptr<tt_tensor> observed_output = workload.queues[q_name].my_io->get();
                    workload.queues[q_name].my_io->pop();
                    if(!determinism_tests){
                        tt_tensor golden_output;
                        if (golden_dir_exists) {
                            golden_output = tt_tensor(md);
                            std::string golden_yaml_path = golden_dir_path + "/";
                            verif::read_tensor_from_binary(golden_yaml_path, q_info, q_info.input_count * loop + b, golden_output, init_rams);
                        } else {
                            golden_output = *golden.pop_output(q_info);
                            if (compare_with_saved_golden && !golden_dir_exists) {
                                std::string golden_yaml_path = golden_dir_path + "/";
                                std::filesystem::create_directories(golden_dir_path);
                                verif::write_tensor_to_binary(golden_yaml_path, q_name, q_info.input_count * loop + b, golden_output);
                            }
                        }
                        log_info(tt::LogTest, "Checking Entry idx={}", b);
                        if (not compare_tensors(golden_output, *observed_output, comp)) {
                            pass = false;
                            log_error("Entry idx={} Mismatched", b);
                        }
                    }

                    else{
                        if(!loop){
                            if(!b){
                                vector<tt_tensor> tensor_queue;
                                init_output_tensors[q_name] = tensor_queue;
                            }
                            init_output_tensors[q_name].push_back(*observed_output);
                        }

                        else{
                            if(not compare_tensors(init_output_tensors[q_name][b], *observed_output, verif::comparison::get_config("determinism_test", test_config.comparison_configs))){
                                pass = false;
                                log_error("Non Deterministic Output: Entry idx={} for output={} in run={} Mismatched with initial output", b, q_name, loop);
                                if(exit_at_failure){
                                    log_info(tt::LogTest, "Exiting at loop={} because of flag exit-at-failure", loop);
                                    goto finish_runtime;
                                }
                            }
                        }
                    }
                }
            }
        }
        log_info(tt::LogTest, "Ended loop {}. ", loop);
    }

    finish_runtime:
        log_info(tt::LogTest, "Calling runtime.finish");
        runtime.finish();

    

    std::string del = "_";
    size_t pos = 0;
    std::string token;
    while ((pos = input_per_sec_thresh_arg.find(del)) != std::string::npos) {
        token = input_per_sec_thresh_arg.substr(0, pos);
        if (!token.empty())
            input_per_sec_thresh.push_back(std::stoi(token));
        input_per_sec_thresh_arg.erase(0, pos + del.length());
    }
    if (!input_per_sec_thresh_arg.empty())
        input_per_sec_thresh.push_back(std::stoi(input_per_sec_thresh_arg));

    vector<tt::EpochPerfInfo> perf_info = runtime.get_perf_info();
    for (int perf_epoch = 0; perf_epoch < perf_info.size(); perf_epoch++) {
        if (input_per_sec_thresh.size() != 0) {
            int thresh_idx = perf_epoch < input_per_sec_thresh.size() ? perf_epoch : input_per_sec_thresh.size()-1;
            if (perf_info[perf_epoch].num_inputs_per_second < input_per_sec_thresh[thresh_idx]) {
                pass = false;
                log_error("Inputs per second = {} is below the threshold {}.", perf_info[perf_epoch].num_inputs_per_second, input_per_sec_thresh[thresh_idx]);
            }
        }
    }

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
}

