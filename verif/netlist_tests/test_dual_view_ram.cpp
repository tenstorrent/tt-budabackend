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
    std::string arch_name = "";
    std::string vcd_dump_cores = "";
    int seed = -1;
    int opt_level = 3;
    bool help = false;
    int param_inner_loop_count = 1;
    int param_outer_loop_count = 1;
    int param_inner_loop_wr_incr = 0;
    int param_outer_loop_wr_incr = 0;
    int param_inner_loop_rd_incr = 0;
    int param_outer_loop_rd_incr = 0;
    int wr_cache_index_start = -1;
    int rd_cache_index_start = -1;
    bool debug_inputs = false;
    bool run_only = false;
};

test_args parse_test_args(std::vector<std::string> input_args) {
    test_args args;
    string help_string;
    help_string += "<test_command> --netlist [netlist_path]\n";
    help_string += "--num-loops <>                  : Number of loops of input_counts to run (Default: 1)\n";
    help_string += "--seed <>                       : Randomization seed (Default: random seed)\n";
    help_string += "--netlist <>                    : Path to netlist file\n";
    help_string += "--backend                       : Backend to run to (Default: Golden)\n";
    help_string += "--compare-to                    : Backend to compare to (Default: None -- Custom expected)\n";
    help_string += "--param-inner-loop-count <>     : Number of inner loop iterations, for netlists that use this param\n";
    help_string += "--param-outer-loop-count <>     : Number of outer loop iterations, for netlists that use this param\n";
    help_string += "--param-inner-loop-wr-incr <>   : Amount to increment write index by per inner loop iter, for netlists that use this param\n";
    help_string += "--param-outer-loop-wr-incr <>   : Amount to increment write index by per outer loop iter, for netlists that use this param\n";
    help_string += "--param-inner-loop-rd-incr <>   : Amount to increment read index by per inner loop iter, for netlists that use this param\n";
    help_string += "--param-outer-loop-rd-incr <>   : Amount to increment read index by per outer loop iter, for netlists that use this param\n";
    help_string += "--wr-cache-index-start <>       : Initial value of wr_cache-index (optional)\n";
    help_string += "--rd-cache-index-start <>       : Initial value of rd_cache-index (optional)\n";

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
            input_args, "--netlist", "verif/netlist_tests/netlists/basic_ram/netlist_dual_view_ram.yaml");
        std::tie(args.vcd_dump_cores, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--vcd-dump-cores", "");
        std::tie(args.backend, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--backend", "Golden");
        std::tie(args.compare_to, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--compare-to", "Invalid");
        std::tie(args.arch_name, input_args) =
            verif_args::get_command_option_and_remaining_args(input_args, "--arch", "grayskull");
        std::tie(args.opt_level, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--O", 4); // Increased from default 0 for Issue #1443
        // For further testing program-looping-on-device with wrpter update on device.
        std::tie(args.param_inner_loop_count, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--param-inner-loop-count", 1);
        std::tie(args.param_outer_loop_count, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--param-outer-loop-count", 1);
        std::tie(args.param_inner_loop_wr_incr, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--param-inner-loop-wr-incr", 0);
        std::tie(args.param_outer_loop_wr_incr, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--param-outer-loop-wr-incr", 0);
        std::tie(args.wr_cache_index_start, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--wr-cache-index-start", -1);
        std::tie(args.rd_cache_index_start, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--rd-cache-index-start", -1);
        std::tie(args.param_inner_loop_rd_incr, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--param-inner-loop-rd-incr", 0);
        std::tie(args.param_outer_loop_rd_incr, input_args) =
            verif_args::get_command_option_int32_and_remaining_args(input_args, "--param-outer-loop-rd-incr", 0);
        std::tie(args.help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, "--help");
        std::tie(args.debug_inputs, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--debug-inputs");
        std::tie(args.run_only, input_args) =
            verif_args::has_command_option_and_remaining_args(input_args, "--run-only");
       
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
            .optimization_level = args.opt_level,
            .output_dir = args.output_dir,
        };
    } else {
        log_fatal("{} is not a supported backend to compile to for this test", args.backend);
    }

    if (args.run_only) {
        backend_config.mode = DEVICE_MODE::RunOnly;
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
    log_assert(target_backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");

    std::shared_ptr<tt_backend> compare_to_backend;
    if (compare_to == tt::DEVICE::Golden) {
        compare_to_backend = tt_backend::create(args.netlist_path, compare_to_config);
        compare_to_backend->initialize();
    }

    // Automatically detect whether this is single program netlist, or split (reader/write programs, graphs). We expected to have
    // separate reader/writer graphs, but lately it has been found that FE is using combined programs. It can work so long
    // as entries being read are not the entries being written by the same graph run.
    tt_runtime_workload &workload = *tt::io::get_workload(args.netlist_path);
    bool single_program_mode = workload.programs.find("write_read_cache") != workload.programs.end();
    log_info(tt::LogTest, "Using single_program_mode: {}", single_program_mode);

    // Restriction: Epected output (non-golden) wasn't updated for loops in netlist.
    int num_loops_in_program = args.param_inner_loop_count * args.param_outer_loop_count;
    if (num_loops_in_program != 1) {
        log_assert(compare_to == tt::DEVICE::Golden, "Must compare with Golden for inner/outer_loop_count>1");
    }

    // TEST START, Initialize RAM to be all 0s
    std::vector<string> cache_names = {"writer", "reader"};  // INPUTS:: FILL-ME
    std::unordered_map<std::string, std::vector<tt_tensor>> inputs(cache_names.size());
    for (const auto& cache_name : cache_names) {
        tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(cache_name);
        tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
        inputs.insert({cache_name, std::vector<tt_tensor>(q_desc.bufq_num_slots)});
        for (int b = 0; b < q_desc.bufq_num_slots; ++b) {
            inputs.at(cache_name).at(b) = tt_tensor(md);
            generate_tensor(VerifStimulusConfig({.type=StimulusType::Constant, .constant_value=0.0f}), inputs.at(cache_name).at(b));
            verif::push_tensor(*target_backend, cache_name, &inputs.at(cache_name).at(b), b);
            if (compare_to_backend) {
                verif::push_tensor(*compare_to_backend, cache_name, &inputs.at(cache_name).at(b), b);
            }
        }
    }

    std::vector<string> output_names = {"output"};  // OUTPUTS:: FILL-ME
    std::vector<string> ram_names = {"reader"};  // OUTPUTS:: FILL-ME

    // Setup slots for expected outputs for "output" and "reader" queue
    std::vector<string> expected_output_names = {"output", "reader"};
    std::unordered_map<std::string, std::vector<tt_tensor>> expected(expected_output_names.size());

    // "reader" cache will be checked entirely, all slots - not just number of tensors that are generated by loops
    // because write index may be outside of this range in recently added single-program tests, and we'd like to check the data.
    for (auto &name : expected_output_names) {
        tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(name);
        int num_tensors = name == "output" ? q_desc.input_count * num_loops_in_program : q_desc.bufq_num_slots;
        expected.insert({name, std::vector<tt_tensor>(num_tensors)});
        for (int b = 0; b < num_tensors; ++b) {
            // Expected Calculation
            tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
            expected.at(name).at(b) = tt_tensor(md);
            generate_tensor(VerifStimulusConfig({.type=StimulusType::Constant, .constant_value=0.0f}), expected.at(name).at(b));
        }
    }

    // Pick reasonable write index
    int t_dim_reader = get_tensor_metadata_for_queue(target_backend->get_queue_descriptor("reader")).shape.z;
    int t_dim_writer = get_tensor_metadata_for_queue(target_backend->get_queue_descriptor("writer")).shape.z;

    int write_cache_idx = args.wr_cache_index_start != -1 ? args.wr_cache_index_start : t_dim_reader / 2; // some valid write dimension
    int read_cache_idx = args.rd_cache_index_start != -1 ? args.rd_cache_index_start : 0;
    int reader_num_entries = target_backend->get_queue_descriptor("reader").bufq_num_slots;
    log_info(tt::LogTest, "t_dim_reader: {} t_dim_writer: {} write_cache_idx: {} reader_num_entries: {}", t_dim_reader, t_dim_writer, write_cache_idx, reader_num_entries);

    for (int n = 0; n < args.num_loops; ++n) {
        log_info(tt::LogTest, "Running test loop n: {} of num_loops: {}", n, args.num_loops);

        // DRIVE INPUTS -- Driven however test wants to set it up
        std::vector<string> input_names = {"q0"};  // INPUTS:: FILL-ME
        std::unordered_map<std::string, std::vector<tt_tensor>> inputs(input_names.size());
        for (const auto &input_name : input_names) {
            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(input_name);
            tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
            int num_tensors = q_desc.input_count * num_loops_in_program;
            inputs.insert({input_name, std::vector<tt_tensor>(num_tensors)});
            log_info(tt::LogTest, "Input {} has input_count: {} num_loops_in_program: {} num_tensors: {}",
                input_name, q_desc.input_count, num_loops_in_program, num_tensors);
            for (int b = 0; b < num_tensors; ++b) {
                inputs.at(input_name).at(b) = tt_tensor(md);
                if (args.debug_inputs) {
                    inputs.at(input_name).at(b).init_to_tile_id(1.0f, (n * 100) + ((b+1) * 16.0f));
                    log_info(tt::LogTest, "Input tensor loop n: {} tensor b: {} is..", n, b);
                    inputs.at(input_name).at(b).dump();
                } else {
                    generate_tensor(verif::stimulus::get_config(input_name, stimulus_configs), inputs.at(input_name).at(b));
                }
                verif::push_tensor(*target_backend, input_name, &inputs.at(input_name).at(b));
                if (compare_to_backend) {
                    verif::push_tensor(*compare_to_backend, input_name, &inputs.at(input_name).at(b));
                }
            }
        }
        
        // Modify write index every loop iteration
        // write_cache_idx = (write_cache_idx + 1) % t_dim_reader;
        int num_tiles_per_cache_idx = inputs.at("q0").at(0).total_tiles();
        int expected_reader_tile_offset = num_tiles_per_cache_idx * write_cache_idx;
        int expected_tile_offset = num_tiles_per_cache_idx * write_cache_idx;

        int batch_size = target_backend->get_queue_descriptor("q0").input_count * num_loops_in_program;

        log_info(tt::LogTest, "Loop: {} Testing batch_size: {} write_cache_idx {} read_cache_idx: {}. Tile offset into reader is {}",
            n, batch_size, write_cache_idx, read_cache_idx, expected_reader_tile_offset);
        
        // RUN_PROGRAM
        std::map<std::string, std::string> program_parameters = {   {"$p_cache_write", std::to_string(write_cache_idx)},
                                                                    {"$p_cache_read", std::to_string(read_cache_idx)},
                                                                    {"$p_inner_loop_count", std::to_string(args.param_inner_loop_count)},
                                                                    {"$p_outer_loop_count", std::to_string(args.param_outer_loop_count)},
                                                                    {"$p_inner_loop_wr_incr", std::to_string(args.param_inner_loop_wr_incr)},
                                                                    {"$p_outer_loop_wr_incr", std::to_string(args.param_outer_loop_wr_incr)},
                                                                    {"$p_inner_loop_rd_incr", std::to_string(args.param_inner_loop_rd_incr)},
                                                                    {"$p_outer_loop_rd_incr", std::to_string(args.param_outer_loop_rd_incr)},
                                                                };

        for (auto &param : program_parameters) {
            log_info(tt::LogTest, "Test Loop: {} using program parameter: {} = {}", n, param.first, param.second);
        }

        target_backend->wait_for_idle();

        // Single program/graph mode added here after finding this is what FE is using. Limited testing, but helped to find/fix a checker bug.
        if (single_program_mode) {

            log_assert(target_backend->run_program("write_read_cache", program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected program write_read_cache to execute successfully on target device");

            // GENERATE EXPECTED OR RUN_PROGRAM FOR COMPARE_TO
            if (compare_to_backend) {
                log_assert(compare_to_backend->run_program("write_read_cache", program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected program write_read_cache to execute successfully on golden device");
            } else {
                log_fatal("Must use --compare-to for single-program mode. Expected output calc not implemented");
            }

            target_backend->wait_for_idle();

            // READ Reader - all entries, not just batch_size entries, since write indices into RAM may be above batch_size.
            for (const auto& output_name : ram_names) {
                log_info(tt::LogTest, "Reading and Checking Reader RAMs.");
                tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
                int num_entries = q_desc.bufq_num_slots;
                tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
                for (int b = 0; b < num_entries; ++b) {
                    tt_tensor output(md);
                    verif::get_and_pop_tensor(*target_backend, output_name, &output, b);
                    if (compare_to_backend) {
                        expected.at(output_name).at(b) = tt_tensor(md);
                        verif::get_and_pop_tensor(*compare_to_backend, output_name, &expected.at(output_name).at(b), b);
                    }

                    log_info(tt::LogTest, "Checking Entry idx={} for output={} of num_entries={}", b, output_name, num_entries);
                    if (not compare_tensors(
                            expected.at(output_name).at(b),
                            output,
                            verif::comparison::get_config(output_name, comparison_configs))) {
                        pass = false;
                        log_error("Entry idx={} for output={} Mismatched", b, output_name);
                    }
                }
            }


        } else {

            log_assert(target_backend->run_program("write_cache", program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected program write_cache to execute successfully on target device");

            // GENERATE EXPECTED OR RUN_PROGRAM FOR COMPARE_TO
            if (compare_to_backend) {
                log_assert(compare_to_backend->run_program("write_cache", program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected program write_cache to execute successfully on golden device");
            } else {
                for (int b = 0; b < batch_size; ++b) {
                    // Each input in batch_size uses same offset
                    expected_reader_tile_offset = num_tiles_per_cache_idx * write_cache_idx;
                    for (const auto& iw: inputs.at(input_names.at(0)).at(b).tile_tensor) {
                        for (const auto& iz: iw) {
                            for (const auto& ir: iz) {
                                for (const auto& input_tile: ir) {
                                    log_info(tt::LogTest, "Writing to reader expected at input_index={}, offset={}. tile[0][0]={}", b, expected_reader_tile_offset, input_tile.t[0][0]);
                                    *expected.at("reader").at(b).get_tile_ptr_from_flat_index(expected_reader_tile_offset, Dim::R) = input_tile;
                                    expected_reader_tile_offset++;
                                }
                            }
                        }
                    }
                }
            }

            // When consuming RAMS, must WFI, since no way to know when data is ready. Without this, race/mismatches with backend_opt>O3
            target_backend->wait_for_idle();

            // READ Reader - all entries, not just batch_size entries, since write indices into RAM may be above batch_size.
            for (const auto& output_name : ram_names) {
                log_info(tt::LogTest, "Reading and Checking Reader RAMs");
                tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
                int num_entries = q_desc.bufq_num_slots;
                tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
                for (int b = 0; b < num_entries; ++b) {
                    tt_tensor output(md);
                    verif::get_and_pop_tensor(*target_backend, output_name, &output, b);
                    if (compare_to_backend) {
                        expected.at(output_name).at(b) = tt_tensor(md);
                        verif::get_and_pop_tensor(*compare_to_backend, output_name, &expected.at(output_name).at(b), b);
                    }

                    log_info(tt::LogTest, "Checking Entry idx={} for output={} of num_entries={}", b, output_name, num_entries);
                    if (not compare_tensors(
                            expected.at(output_name).at(b),
                            output,
                            verif::comparison::get_config(output_name, comparison_configs))) {
                        pass = false;
                        log_error("Entry idx={} for output={} Mismatched", b, output_name);
                    }
                }
            }

            target_backend->wait_for_idle();
            log_assert(target_backend->run_program("read_cache", program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected program read_cache to execute successfully on target device");
            // GENERATE EXPECTED OR RUN_PROGRAM FOR COMPARE_TO
            if (compare_to_backend) {
                log_assert(compare_to_backend->run_program("read_cache", program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected program read_cache to execute successfully on golden device");
            } else {
                for (int b = 0; b < batch_size; ++b) {
                    expected_tile_offset = num_tiles_per_cache_idx * write_cache_idx;

                    for (const auto& iw: inputs.at(input_names.at(0)).at(b).tile_tensor) {
                        for (const auto& iz: iw) {
                            for (const auto& ir: iz) {
                                for (const auto& input_tile: ir) {
                                    log_info(tt::LogTest, "Writing to output expected at input_index={}, offset={}", b, expected_tile_offset);
                                    *expected.at("output").at(b).get_tile_ptr_from_flat_index(expected_tile_offset, Dim::R) = input_tile;
                                    expected_tile_offset++;
                                }
                            }
                        }
                    }
                }
            }

        }


        // READ OUTPUTS
        for (const auto& output_name : output_names) {
            log_info(tt::LogTest, "Reading and Checking Output Queues");
            int timeout_in_seconds = 1;
            tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(output_name);
            tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);
            for (int b = 0; b < batch_size; ++b) {
                tt_tensor output(md);
                log_info(tt::LogTest, "Getting Entry idx={} for output={} of batch_size={}", b, output_name, batch_size);
                verif::get_and_pop_tensor(*target_backend, output_name, &output, b, timeout_in_seconds);
                if (compare_to_backend) {
                    expected.at(output_name).at(b) = tt_tensor(md);
                    verif::get_and_pop_tensor(*compare_to_backend, output_name, &expected.at(output_name).at(b), b);
                }
                log_info(tt::LogTest, "Checking Entry idx={} for output={} of batch_size={}", b, output_name, batch_size);
                if (not compare_tensors(
                        expected.at(output_name).at(b),
                        output,
                        verif::comparison::get_config(output_name, comparison_configs))) {
                    pass = false;
                    log_error("Entry idx={} for output={} Mismatched", b, output_name);
                }
            }
        }

        // Modify write index every loop iteration. If single-program mode, need to also modify reader start
        // otherwise test intention would be violated, and reader would end up reading same data written by writer
        // which is race/not-allowed (runtime assert added to catch it via check_for_dual_view_ram_rd_wr_overlap_in_graph())
        // After some thought, I think these incrment amounts make sense.
        if (single_program_mode) {
            write_cache_idx = (write_cache_idx + t_dim_reader) % reader_num_entries;
            read_cache_idx = (read_cache_idx + t_dim_writer) % reader_num_entries;
        } else {
            write_cache_idx = (write_cache_idx + 1) % t_dim_reader;
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