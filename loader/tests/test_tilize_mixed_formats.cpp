// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched, DataFormat host_format) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = batched ? queue_info.input_count : 1;
    md.is_tilized = true;

    md.data_format = host_format;

    tt_tensor tensor(md);
    tensor.init_to_tile_id(1.0f, entry_idx*16.0f);
    return tensor;
}

void pop_and_check_output(tt_runtime_workload &workload, vector<tt_dram_io_desc> &in_desc, tt_dram_io_desc &pop_desc, tt_cluster *cluster, map<std::string, vector<tt_tensor>> &input_tensors_map) {
    bool match = true;
    tt_queue_info &queue_info = workload.queues[pop_desc.queue_name].my_queue_info;
    vector<tt_tensor> output_tensors;

    // Pop all outputs until queues are empty
    int output_idx = 0;
    while (!tt::io::is_queue_empty(pop_desc, queue_info, cluster)) {
        vector<uint32_t> rv = tt::io::pop_untilized_vector(pop_desc, queue_info, cluster, 0 /*timeout_in_seconds*/);
        log_info(tt::LogTest, "Untilizer output queue {} pop #{}, device data_format={}", queue_info.name, output_idx, queue_info.data_format);
        output_tensors.push_back(tt::io::reconstruct_tensor_from_untilized(rv, pop_desc, false/*batched*/, false /*2s complement*/, DataFormat::Invalid /*from format*/, 1 /*batch*/));
        output_idx++;
    }

    // Perform checks as a single batched tensor (stack along W-dim)
    tt_tensor op0 = tt_tensor::wstack(input_tensors_map[in_desc.at(0).queue_name]);
    tt_tensor op1 = tt_tensor::wstack(input_tensors_map[in_desc.at(1).queue_name]);
    tt_tensor observed = tt_tensor::wstack(output_tensors);
    tt_tensor expected = op0 + op1;

    if (observed != expected) {
        for (int wi = 0; wi < expected.getw(); ++wi) {
            for (int zi = 0; zi < expected.getz(); ++zi) {
                for (int ri = 0; ri < expected.getrt(); ++ri) {
                    for (int ci = 0; ci < expected.getct(); ++ci) {
                        if (expected.tile_tensor[wi][zi][ri][ci] != observed.tile_tensor[wi][zi][ri][ci]) {
                            log_info(tt::LogTest, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                            log_info(tt::LogTest, "Diff\n{}", expected.tile_tensor[wi][zi][ri][ci].get_diff_string(observed.tile_tensor[wi][zi][ri][ci]));
                        }
                    }
                }
            }
        }
        match = false;
    }
    log_assert(match, "Tile mismatch!");
    log_info(tt::LogTest, "Queue result check PASSED!", queue_info.name);
}


// Ensure HW Tilizer was used in tests that intended it to be used, when using automatic enablement of hw-tilize.
void ensure_hw_tilize_used(tt_runtime_config &config, vector<tt_dram_io_desc> &input_io_desc){
    for (const auto& desc: input_io_desc) {
        bool hw_tilizer_exists = tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::exists(desc.queue_name) or tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::exists(desc.queue_name);
        log_assert(hw_tilizer_exists, "HW Tilizer was expected to be used in this test but was not, constraints violated?");
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    int opt_level            = verif_args::get_command_option_uint32(args, "--O", 1);
    int num_pushes           = verif_args::get_command_option_uint32(args, "--num-pushes", 1);
    bool batched_push        = verif_args::has_command_option(args, "--batched-push");
    bool run_silicon         = verif_args::has_command_option(args, "--silicon");
    std::string netlist_path = verif_args::get_command_option(args, "--netlist", "loader/tests/net_tilize_untilize/binary_hw_tilize_untilize.yaml");

    std::string arch_name;
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    perf::PerfDesc perf_desc(args, netlist_path);
    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);
    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();

    // Runtime init
    tt_device_params device_params;
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

    // Host fast tilize and push
    int input_index = 0;

    // Generate input tensors up front - needed, to use wrapper API for hw/sw tilize when it inspects data format.
    map<std::string, vector<tt_tensor>> input_tensors_map;
    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();

    for (const auto& desc: input_io_desc) {
        tt_queue_info &queue_info = workload.queues[desc.queue_name].my_queue_info;

        // Feels like this is a real requirement here... It was hang without batched_push=1.
        if (num_pushes == 1 && queue_info.input_count > 1){
            log_assert(batched_push , "Must set batched_push=1 when num_pushes=1 and input_count>1");
        }

        for (int entry_idx=0; entry_idx<num_pushes; entry_idx++) {

            // Keep mixing up which input uses a fp32 host tensor, which requires tilizer to convert to device format
            DataFormat host_format = (input_index++ % 3 == 0) ? DataFormat::Float32 : DataFormat::Float16_b;
            tt_tensor input = get_tilized_tensor(queue_info, entry_idx, batched_push, host_format);
            input_tensors_map[queue_info.name].push_back(input);
        }
    }

    tt::io::push_host_inputs(input_io_desc, [&](tt_queue_info& queue_info, int entry_idx){
        return input_tensors_map[queue_info.name].at(entry_idx);
    }, num_pushes, false);


    // Make sure HW Tilizer was actually used and yaml doesn't violate constraints.
    ensure_hw_tilize_used(config, input_io_desc);

    // Interactively run programs
    for (auto const& program : workload.programs) {
        runtime.run_program(program.first, {});
    }

    // Collect outputs via lazy method that returns all output queues
    vector<tt_dram_io_desc> output_io_desc = runtime.get_device_output_io_desc();
    for (tt_dram_io_desc &io_desc : output_io_desc) {
        pop_and_check_output(workload, input_io_desc, io_desc, runtime.cluster.get(), input_tensors_map);
    }

    runtime.finish();
    return 0;
}

