// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = 1;
    md.is_tilized = true;
    md.data_format = queue_info.data_format;

    tt_tensor tensor(md);
    tensor = 1.0f;
    return tensor;
}

int main(int argc, char **argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_netlist_program since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);

    std::vector<std::string> args(argv, argv + argc);
    // Runtime setup
    tt_runtime_config config(args);

    verif_args::validate_remaining_args(args);

    // config.device_params = {.vcd_dump_cores = {"1-1"}};
    tt_runtime runtime(config.netlist_path, config);

    // Start device and statically assemble binaries
    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized succesfully");

    bool force_sw_tilize = true;

    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    tt::io::push_host_inputs(input_io_desc, &get_tilized_tensor, -1, force_sw_tilize);

    // Interactively run programs
    for (auto const& program : runtime.get_workload()->programs) {
        log_assert(runtime.run_program(program.first, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to be run succesfully");
    }
    // Shutdown device
    log_assert(runtime.finish() == tt::DEVICE_STATUS_CODE::Success, "Expected device to be closed succesfully");

    // // Get performance data
    // vector<tt::EpochPerfInfo> perf = runtime.get_perf_info();
    // log_info(tt::LogTest, "|{:-^20}|{:-^20}|{:-^20}|", "Program", "Graph", "Tensors/s");
    // for (auto &epoch : perf) {
    //     log_info(tt::LogTest, "|{:^20}|{:^20}|{:^20.2f}|", epoch.program_name, epoch.graph_name, epoch.num_inputs_per_second);
    // }
    return 0;
}

