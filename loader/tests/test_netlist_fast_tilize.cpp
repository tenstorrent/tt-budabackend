// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>
#include <chrono>
#include <ctime>

#include "runtime.hpp"

#include "verif.hpp"
using namespace verif::comparison;

struct RandomConfig {
    bool generate_random_vals = false;
    int spread = 127;
    int man_variance_bits = 23;
};

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, DataFormat host_format, RandomConfig random_config, bool init_to_zero) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = 1;
    md.is_tilized = true;

    // Intentionaly ignore queue_info.data_format but instead
    // pretend that we're a host pytorch tensor using a different format
    md.data_format = host_format;

    float base_value = (entry_idx%2) ? -16.0f : 16.0f;
    float step_size  = (entry_idx%2) ? -1.0f : 1.0f;

    tt_tensor tensor(md);
    if (init_to_zero) {
        tensor.init_to_input_value(0.0);
    } else if (random_config.generate_random_vals) {
        tensor.randomize_manual_float(random_config.spread, random_config.man_variance_bits);
    } else {
        tensor.init_to_tile_id(1.0f, 0);
    }
    tensor.pack_data();
    tensor.unpack_data();
    return tensor;
}

void pop_and_check_output(tt_runtime_workload &workload, tt_dram_io_desc &pop_desc, tt_cluster *cluster, std::vector<tt_tensor> &expected_output, VerifComparisonConfig comparison_config) {
    static int output_idx = 0;
    tt_queue_info &queue_info = workload.queues[pop_desc.queue_name].my_queue_info;
    bool compared_tiles = false;

    while (!tt::io::is_queue_empty(pop_desc, queue_info, cluster)) {
        vector<uint32_t> rv = tt::io::pop_untilized_vector(pop_desc, queue_info, cluster, 0 /*timeout_in_seconds*/);
        log_info(tt::LogTest, "Untilizer output queue {} pop #{} from loc {}, device data_format={}", queue_info.name, output_idx, queue_info.loc, queue_info.data_format);

        tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(rv, pop_desc, false/*batched*/, false /*2s complement*/, DataFormat::Invalid /*from format*/, 1 /*batch*/);
        tt_tensor expected = expected_output.at(output_idx);

        if (not compare_tensors(expected, observed, comparison_config)) {
            log_fatal("Tile mismatch");
        }
        compared_tiles = true;
        output_idx++;
    }
    log_assert(compared_tiles, "Finished pop_and_check_output() but did not compare any tiles");
    log_info(tt::LogTest, "Queue '{}' result check PASSED!", queue_info.name);
}

std::vector<tt_tensor> pop_from_queue(tt_runtime_workload &workload, tt_dram_io_desc &pop_desc, tt_cluster *cluster)
{
    std::vector<tt_tensor> queue_contents;
    tt_queue_info &queue_info = workload.queues[pop_desc.queue_name].my_queue_info;
    while (!tt::io::is_queue_empty(pop_desc, queue_info, cluster)) {
        tt_tensor observed = tt::io::pop_queue_tilized_output(queue_info, cluster, true, 1 /*pop_count*/, Dim::R /*ublock_scan*/, 0 /*timeout_in_seconds*/); //unpack done here
        //observed.unpack_data();
        queue_contents.push_back(observed);
    }
    log_assert(!queue_contents.empty(), "Queue was empty.");
    return queue_contents;
}

void compare_queue_observed_to_expected(std::vector<tt_tensor> &observed_input, std::vector<tt_tensor> &expected_input, std::string label, VerifComparisonConfig comparison_config)
{
    log_assert(observed_input.size() == expected_input.size(), "Observed and Expected Input vectors of tensors do not have the same size.");
    log_assert(observed_input.size() > 0, "Observed input size should be greater than zero");

    log_info(tt::LogTest, "Going to compare observed vs expected for {}.", label);

    for (int input_idx=0; input_idx< observed_input.size(); input_idx++){
        tt_tensor observed = observed_input.at(input_idx);
        tt_tensor expected = expected_input.at(input_idx);
        if (not compare_tensors(expected, observed, comparison_config)){
            log_info(tt::LogTest, "Observed Tensor for {}", label);
            observed.dump();
            log_info(tt::LogTest, "Expected Tensor for {}", label);
            expected.dump();
            std::string msg = "Tile mismatch between Observed and Expected for " + label + " . See preceding dumps.";
            log_fatal("Tile mismatch between Observed and Expected for {} . See preceding dumps.", label);
        }
        log_debug(tt::LogTest, "Finished comparing input_idx: {} observed vs expected for {}.", input_idx, label);
    }
}


// HW Tilizer has locally cached pointers, and also is constructed with wrptr=0/rdptr=0 which is not accurate for device
// if sw-tilizer was used first. To workaround these issues, re-allocate io queues and destroy hw tilizer from object cache.
void reallocate_io_queues_and_destroy_hw_tilizer(tt_runtime_config &config, tt_runtime &runtime, tt_runtime_workload &workload){
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::destroy();
    tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::destroy();
    runtime.loader->create_and_allocate_io_queues(workload.queues);
}

// Ensure HW Tilizer was used in tests that intended it to be used, when using automatic enablement of hw-tilize.
void ensure_hw_tilize_used(tt_runtime_config &config, vector<tt_dram_io_desc> &input_io_desc){
    for (const auto& desc: input_io_desc) {
        bool hw_tilizer_exists = tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::exists(desc.queue_name) or tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::exists(desc.queue_name);
        log_assert(hw_tilizer_exists, "HW Tilizer was expected to be used in this test but was not, constraints violated?");
    }
}

void push_to_hw_and_sw_tilize_pop_and_compare(tt_runtime_config &config, tt_runtime &runtime, tt_runtime_workload &workload, std::string netlist_path,
    vector<tt_dram_io_desc> &input_io_desc, std::vector<tt_tensor> &input_tensors, bool skip_run, VerifComparisonConfig comparison_config, bool hw_tilize_check,
    std::vector<tt_tensor> &all_zero_tensors)
{

    int total_tensor_size = 0;
    int input_idx = 0;
    for (tt_dram_io_desc &io : input_io_desc) {
        tt_queue_info &queue_info   = workload.queues.at(io.queue_name).my_queue_info;
        auto device_data_format     = queue_info.data_format;
        int num_inputs              = queue_info.entries;
        for (int input = 0; input < num_inputs; input++) {
            total_tensor_size += input_tensors.at(input_idx).size_bytes(device_data_format);
            input_idx++;
        }
    }
    log_assert(input_idx == input_tensors.size(), "There must be num_entries pre-generated tensors for each io descriptor");
    
    int queue_header_size = tt::io::io_queue_header_size_bytes;
    // Step 1: SW (Slow) Tilize and Push
    auto sw_start = std::chrono::high_resolution_clock::now();
    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        log_debug(tt::LogTest, "Getting input tensor to push for SW Tilize with entry_idx: {}", entry_idx);
        return input_tensors.at(entry_idx);
    }, -1, true); // force_sw_tilize=true
    auto sw_end = std::chrono::high_resolution_clock::now();

    // Records each byte for sw and hw tilizer
    vector<uint8_t> sw_tilize_bytes;
    vector<uint8_t> hw_tilize_bytes;

    if (hw_tilize_check) {
        for (tt_dram_io_desc &io : input_io_desc) {
            int num_cores = io.bufq_mapping.size();
            for (auto bufq_addr: io.bufq_mapping) {
                uint8_t* output_addr = reinterpret_cast<uint8_t*>(bufq_addr);
                log_assert(total_tensor_size % num_cores == 0, "Tensor size mismatch");
                for (int byte = 0; byte < total_tensor_size / num_cores + queue_header_size; byte++) {
                    sw_tilize_bytes.push_back(*(output_addr + byte));
                }
            }
        }
    }
    std::vector<tt_tensor> sw_tilize_input_queue_tensors = pop_from_queue(workload, input_io_desc[0], runtime.cluster.get());

    // Push all zero tensors to reset the values in dram before trying hw_tilize
    // This will ensure the values read from hw-tilize pop are not sw-tilize numbers
    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        log_debug(tt::LogTest, "Getting input tensor to push for SW Tilize and reset values with entry_idx: {}", entry_idx);
        return all_zero_tensors.at(entry_idx);
    }, -1, true); // force_sw_tilize=true
    std::vector<tt_tensor> sw_tilize_temp = pop_from_queue(workload, input_io_desc[0], runtime.cluster.get());


    reallocate_io_queues_and_destroy_hw_tilizer(config, runtime, workload);

    // Step 2: HW (Fast) Tilize and Push
    auto hw_start = std::chrono::high_resolution_clock::now();
    // tt::io::push_host_inputs_with_hw_tilize(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        log_debug(tt::LogTest, "Getting input tensor to push for HW Tilize with entry_idx: {}", entry_idx);
        return input_tensors.at(entry_idx);
    }, -1);
    auto hw_end = std::chrono::high_resolution_clock::now();

    if (hw_tilize_check) {
        for (tt_dram_io_desc &io : input_io_desc) {
            int num_cores = io.bufq_mapping.size();
            for (auto bufq_addr: io.bufq_mapping) {
                uint8_t* output_addr = reinterpret_cast<uint8_t*>(bufq_addr);
                log_assert(total_tensor_size % num_cores == 0, "Invalid tensor size");
                for (int byte = 0; byte < total_tensor_size / num_cores + queue_header_size; byte++) {
                    hw_tilize_bytes.push_back(*(output_addr + byte));
                }
            }
        }
        log_assert(sw_tilize_bytes.size() == hw_tilize_bytes.size(), "SW Tilize HW Tilize Byte size mismatch");
        // std::ofstream sw_tilize_file("sw_tilize.txt");
        // std::ofstream hw_tilize_file("hw_tilize.txt");
        // int counter = 0;
        // sw_tilize_file << "Queue Header" << endl;
        // hw_tilize_file << "Queue Header" << endl;
        // for (int byte = 0; byte< queue_header_size; byte++) {
        //     sw_tilize_file << (uint32_t)sw_tilize_bytes.at(byte) << endl;
        //     hw_tilize_file << (uint32_t)hw_tilize_bytes.at(byte) << endl;
        // }
        for (int byte = 0; byte < sw_tilize_bytes.size(); byte++) {
            // sw_tilize_file << (uint32_t)sw_tilize_bytes.at(byte) << endl;
            // hw_tilize_file << (uint32_t)hw_tilize_bytes.at(byte) << endl;
            log_assert(sw_tilize_bytes.at(byte) == hw_tilize_bytes.at(byte), "SW Tilize HW Tilize Mismatch at byte {}", byte);
        }
        log_info(tt::LogTest, "All {} bytes between sw and hw tilizer outputs match", sw_tilize_bytes.size());
    }

    int64_t sw_tilize_exec_time = std::chrono::duration_cast<std::chrono::microseconds>(sw_end - sw_start).count();
    int64_t hw_tilize_exec_time = std::chrono::duration_cast<std::chrono::microseconds>(hw_end - hw_start).count();

    // Make sure HW Tilizer was actually used and yaml doesn't violate constraints.
    ensure_hw_tilize_used(config, input_io_desc);

    std::vector<tt_tensor> hw_tilize_input_queue_tensors = pop_from_queue(workload, input_io_desc[0], runtime.cluster.get());
    
    // In comparison between hw/sw tilize, we need perfect match
    VerifComparisonConfig comparison_config_exact_match = comparison_config;
    comparison_config_exact_match.user_configured = true;
    comparison_config_exact_match.type = ComparisonType::Exact;

    // Step 3 : Execute comparisons.
    // FIXME - Currently the queue header, and tile headers are not checked, only the data
    // Idea: Compare just the queue headers, and first tile headers.
    compare_queue_observed_to_expected(hw_tilize_input_queue_tensors, sw_tilize_input_queue_tensors, "hw_tilize_vs_sw_tilize", comparison_config_exact_match);
    if (!hw_tilize_check) {
        compare_queue_observed_to_expected(hw_tilize_input_queue_tensors, input_tensors, "hw_tilize_vs_orig_input_tensors", comparison_config);
        compare_queue_observed_to_expected(sw_tilize_input_queue_tensors, input_tensors, "sw_tilize_vs_orig_input_tensors", comparison_config);
    }

    log_info(tt::LogTest, "SW-Tilize execution time = {} uS", sw_tilize_exec_time);
    log_info(tt::LogTest, "HW-Tilize execution time = {} uS", hw_tilize_exec_time);
    log_info(tt::LogTest, "SW-Tilize BW = {} MB/S", (float(total_tensor_size)/(1024*1024)) / (sw_tilize_exec_time/1000000.0));
    log_info(tt::LogTest, "HW-Tilize BW = {} MB/S", (float(total_tensor_size)/(1024*1024)) / (hw_tilize_exec_time/1000000.0));

    if (!skip_run){
        reallocate_io_queues_and_destroy_hw_tilizer(config, runtime, workload);
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    int opt_level;
    bool run_silicon;
    bool hw_tilize_check;
    std::string netlist_path;
    std::string host_format_;
    int seed;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 2);
    std::tie(run_silicon, args) = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_tilize_untilize/tilize_fp16b_untilize_fp16.yaml");
    std::tie(host_format_, args) = verif_args::get_command_option_and_remaining_args(args, "--host-data-format", "Float32");
    // If following flag is set, will only compare every single byte in sw-tilize output to hw-tilize output including queue header, tile headers, and exponent bits
    // If following flag is set, will directly read the data from the location the tilizer has written the data into
    // If following flag is set, will skip hw-tilizer output check vs input tensor
    std::tie(hw_tilize_check, args) = verif_args::has_command_option_and_remaining_args(args, "--hw-tilize-check");
    std::tie(seed, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", -1);

    // Ideally want to use these flags, but running into some hangs,
    bool skip_run;
    bool tilize_comparison;
    bool randomize_tensor;
    std::tie(skip_run, args) = verif_args::has_command_option_and_remaining_args(args, "--skip-run");
    std::tie(tilize_comparison, args) = verif_args::has_command_option_and_remaining_args(args, "--tilize-comparison");
    std::tie(randomize_tensor, args) = verif_args::has_command_option_and_remaining_args(args, "--randomize-tensor");

    DataFormat host_format   = verif::STRING_TO_DATA_FORMAT.at(host_format_);

    std::string arch_name;
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    perf::PerfDesc perf_desc(args, netlist_path);
    VerifComparisonConfig comparison_config = verif::comparison::read_from_yaml_file(netlist_path);
    // Check to see if test-config is defined in netlist
    if (comparison_config.type == ComparisonType::Invalid) {
        comparison_config = VerifComparisonConfig(ComparisonType::AllClose, ComparisonVerbosity::AllFails);
    }
    RandomConfig random_config;
    if (randomize_tensor) {
        random_config.generate_random_vals = true;
        if (seed == -1) {
            seed = uint(std::time(NULL));
        }
        log_info(tt::LogTest, "Generating random tensor with seed {}", seed);
        verif::random::tt_rnd_set_seed(seed);
    }

    verif_args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);
    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();

    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

    // Generate input tensors up front - needed, to use wrapper API for hw/sw tilize when it inspects data format.
    std::vector<tt_tensor> input_tensors;
    std::vector<tt_tensor> all_zero_tensors;
    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    for (const auto& desc: input_io_desc) {
        tt_queue_info &queue_info = workload.queues[desc.queue_name].my_queue_info;
        for (int i=0; i<queue_info.entries; i++) {
            input_tensors.push_back(get_tilized_tensor(queue_info, i, host_format, random_config, false));
            all_zero_tensors.push_back(get_tilized_tensor(queue_info, i, host_format, random_config, true));
        }
    }

    // Optional Comparison betwen SW-Tilize vs HW-Tilize, SW-Tilize vs Original, and HW-Tilize vs Original.
    if (tilize_comparison)
        push_to_hw_and_sw_tilize_pop_and_compare(config, runtime, workload, netlist_path, input_io_desc, input_tensors, skip_run, comparison_config, hw_tilize_check, all_zero_tensors);

    // In case we want to have a test that just executes push/pop tilize comparison, and does not run.
    if (!skip_run){

        // Host fast tilize and push
        tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
            return input_tensors.at(entry_idx);
        }, -1, false);

        // Make sure HW Tilizer was actually used and yaml doesn't violate constraints.
        ensure_hw_tilize_used(config, input_io_desc);

        // Interactively run programs
        for (auto const& program : workload.programs) {
            runtime.run_program(program.first, {});
        }

        runtime.wait_for_idle();

        // Collect outputs using untilizer, optionally migrate and dma queue contents to host
        vector<tt_dram_io_desc> output_io_desc = runtime.get_device_output_io_desc();
        for (tt_dram_io_desc &io_desc : output_io_desc) {
            pop_and_check_output(workload, io_desc, runtime.cluster.get(), input_tensors, comparison_config);
        }
    }

    runtime.finish();
    return 0;
}

