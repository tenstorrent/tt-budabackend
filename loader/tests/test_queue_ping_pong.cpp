// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx) {
    static float offset = 0.0f;
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = 1;
    md.is_tilized = true;
    md.data_format = queue_info.data_format;

    tt_tensor tensor(md);
    tensor.init_to_tile_id(1.0f, offset);
    offset += 16.0f;
    return tensor;
}

void pop_and_check_output(tt_runtime_workload &workload, tt_dram_io_desc &pop_desc, tt_cluster *cluster, std::vector<tt_tensor> &expected_output) {
    static int output_idx = 0;
    tt_queue_info &queue_info = workload.queues[pop_desc.queue_name].my_queue_info;

    while (!tt::io::is_queue_empty(pop_desc, queue_info, cluster)) {
        tt_tensor observed = tt::io::pop_queue_tilized_output(queue_info, cluster, true, 1 /*pop_count*/, Dim::R /*ublock_scan*/, 0 /*timeout_in_seconds*/);
        //observed.unpack_data();
        tt_tensor expected = expected_output.at(output_idx);
        log_assert(observed == expected, "Tile mismatch!");
        output_idx++;
    }
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

    bool run_silicon;
    int cache_hits;
    std::string netlist_path;
    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_basic/netlist_unary.yaml");
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");

    std::string arch_name;
    std::tie(arch_name, args) = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    perf::PerfDesc perf_desc(args, netlist_path);
    verif_args::validate_remaining_args(args);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, 0, run_silicon);
    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();
 
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    vector<tt_dram_io_desc> output_io_desc = runtime.get_device_output_io_desc();
    vector<tt_tensor> input_tensors;
    int input_tensor_batch_offset = 0;

    // Run program multiple times, fill and drain the whole double buffered input queue each batch
    for (int batch=0; batch<8; batch++) {

        log_info(tt::LogTest, "Running batch {}...", batch);

        // Generate input tensors (expected output) up front - needed, to use wrapper API for hw/sw tilize when it inspects data format.
        for (const auto& desc: input_io_desc) {
            tt_queue_info &queue_info = workload.queues[desc.queue_name].my_queue_info;
            for (int entry_idx=0; entry_idx<queue_info.entries; entry_idx++) {
                input_tensors.push_back(get_tilized_tensor(queue_info, entry_idx));
            }
        }

        tt::io::push_host_inputs(input_io_desc, [&input_tensors, &input_tensor_batch_offset](tt_queue_info& queue_info, int entry_idx){
            return input_tensors.at(input_tensor_batch_offset + entry_idx); // expected = input for datacopy
        }, -1, false);

        // Make sure HW Tilizer was actually used and yaml doesn't violate constraints.
        ensure_hw_tilize_used(config, input_io_desc);

        // if (batch % 2) {
        //     runtime.run_program("ping", {});
        //     pop_and_check_output(workload, output_io_desc[0], runtime.cluster.get(), input_tensors);
        //     runtime.run_program("pong", {});
        //     pop_and_check_output(workload, output_io_desc[0], runtime.cluster.get(), input_tensors);
        // } else {
        //     runtime.run_program("ping_pong", {});
        //     pop_and_check_output(workload, output_io_desc[0], runtime.cluster.get(), input_tensors);
        // }
        runtime.run_program("ping_pong", {});
        pop_and_check_output(workload, output_io_desc[0], runtime.cluster.get(), input_tensors);

        input_tensor_batch_offset = input_tensors.size(); // Update for next batch loop's push
    }
    runtime.finish();
    return 0;
}

