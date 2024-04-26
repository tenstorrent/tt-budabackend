// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = batched ? queue_info.input_count : 1;
    md.is_tilized = true;
    md.data_format = queue_info.data_format;

    tt_tensor tensor(md);
    tensor.init_to_tile_id(1.0f, entry_idx*16.0f);
    return tensor;
}

void adapt_4d_tensor_to_3d_tensor(tt::tt_PytorchTensorDesc &py_tensor_desc, bool dim_only) {
    log_info(tt::LogTest, "Tensor b/f {}, dim={} shape=[{},{},{},{}], strides=[{},{},{},{}]",
        __FUNCTION__,
        py_tensor_desc.dim,
        py_tensor_desc.shape[0],
        py_tensor_desc.shape[1],
        py_tensor_desc.shape[2],
        py_tensor_desc.shape[3],
        py_tensor_desc.strides[0],
        py_tensor_desc.strides[1],
        py_tensor_desc.strides[2],
        py_tensor_desc.strides[3]);

    py_tensor_desc.dim = 3;

    if (!dim_only) {
        int wdim = py_tensor_desc.shape[0];
        int zdim = py_tensor_desc.shape[1];
        log_assert(zdim == 1, "Dimensionality reduction cannot be performed when z-dim is non-zero!");
        py_tensor_desc.shape[0] = 1;
        py_tensor_desc.shape[1] = wdim;

        int wstride = py_tensor_desc.strides[0];
        int zstride = py_tensor_desc.strides[1];
        log_assert(zstride == wstride, "Dimensionality reduction cannot be performed when outermost strides are not equal!");
        wstride = wdim*zstride;
        py_tensor_desc.strides[0] = wstride;
        py_tensor_desc.strides[1] = zstride;
    }

    log_info(tt::LogTest, "Tensor a/f {}, dim={} shape=[{},{},{},{}], strides=[{},{},{},{}]",
        __FUNCTION__,
        py_tensor_desc.dim,
        py_tensor_desc.shape[0],
        py_tensor_desc.shape[1],
        py_tensor_desc.shape[2],
        py_tensor_desc.shape[3],
        py_tensor_desc.strides[0],
        py_tensor_desc.strides[1],
        py_tensor_desc.strides[2],
        py_tensor_desc.strides[3]);
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;

    int opt_level, num_pushes;
    bool run_silicon, batched_push, skip_tile_check;
    bool io_api_timeout_test;
    std::string netlist_path;
    std::string arch_name;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 2);
    std::tie(num_pushes, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-pushes", 1);
    std::tie(batched_push, args) = verif_args::has_command_option_and_remaining_args(args, "--batched-push");
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_basic/netlist_unary_host_queue.yaml");
    std::tie(arch_name, args)    = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");

    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    verif_args::validate_remaining_args(args);

    // Backend setup
    tt_backend_config config = {
        .type = run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = arch_name == "grayskull" ? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE,
        .optimization_level = opt_level,
        .output_dir = output_dir};

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);

    // ----------------------------------------------------------------
    // PyBuda API for initializing backend
    // ----------------------------------------------------------------
    log_assert(backend.initialize() == tt::DEVICE_STATUS_CODE::Success, "initialize failed");

    // ----------------------------------------------------------------
    // PyBuda API for querying IO descriptors
    // ----------------------------------------------------------------
    vector<tt_dram_io_desc> input_io_desc;
    for (const auto& it : workload.queues) {
        if (it.second.my_queue_info.input == "HOST") {
            tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
            tt::backend::translate_addresses(io_desc);
            input_io_desc.push_back(io_desc);
        }
    }

    std::vector<tt_tensor> expected_output;

    log_info(tt::LogTest, "Pushing all inputs...");

    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);

    for (tt_dram_io_desc &io_desc : input_io_desc) {
        tt_queue_info& queue_info = workload.queues[io_desc.queue_name].my_queue_info;
        std::uint32_t batch_size = batched_push ? queue_info.input_count : 1;

        std::array<std::uint32_t, PY_TENSOR_DIMS> shape_4d = {
            (uint32_t)batch_size,
            (uint32_t)queue_info.dim.t,
            (uint32_t)queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c * tt::constants::TILE_WIDTH,
            (uint32_t)queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r * tt::constants::TILE_HEIGHT};

        std::array<std::uint32_t, PY_TENSOR_DIMS> shape_3d = {
            1,
            batch_size,
            queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c * tt::constants::TILE_WIDTH,
            queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r * tt::constants::TILE_HEIGHT};

        for (int i=0; i<num_pushes; i++) {
            // Set up input and expected output, identity for unary datacopy
            tt_tensor input = get_tilized_tensor(queue_info, i, batched_push);
            expected_output.push_back(input);

            // Mimic python runtime to generate input tensor to pytorch tensor descriptor
            aligned_vector<float> host_stored_tensor;
            aligned_vector<uint16_t> host_stored_tensor_half;
            tt::tt_PytorchTensorDesc py_tensor_desc = input.get_data_format() == DataFormat::Float32 ? 
                tt::io::get_pytorch_tensor_desc<float>(input, host_stored_tensor) :
                tt::io::get_pytorch_tensor_desc<uint16_t>(input, host_stored_tensor_half);
            py_tensor_desc.owner = tt::OWNERSHIP::Frontend;

            log_assert(py_tensor_desc.shape == shape_4d, "Invalid shape");
            adapt_4d_tensor_to_3d_tensor(py_tensor_desc, false);

            // ----------------------------------------------------------------
            // PyBuda API for pushing input
            // ----------------------------------------------------------------
            auto push_status = tt::backend::push_input(io_desc, py_tensor_desc, !batched_push, 1);
            log_assert(py_tensor_desc.shape == shape_3d, "Invalid shape");
            log_assert(push_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on this push");
        }
    }

    // ----------------------------------------------------------------
    // PyBuda API for running programs
    // ----------------------------------------------------------------
    for (auto const& program : workload.programs) {
        backend.run_program(program.first, {});
    }
    backend.wait_for_idle();

    // ----------------------------------------------------------------
    // PyBuda API for querying IO descriptors
    // ----------------------------------------------------------------
    vector<tt_dram_io_desc> output_io_desc;
    for (const auto& it : workload.queues) {
        if (it.second.my_queue_info.input != "HOST") {
            tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
            tt::backend::translate_addresses(io_desc);
            output_io_desc.push_back(io_desc);
        }
    }

    for (tt_dram_io_desc &io_desc : output_io_desc) {
        int output_idx = 0;

        tt_queue_info& queue_info = workload.queues[io_desc.queue_name].my_queue_info;
        std::uint32_t batch_size = batched_push ? queue_info.input_count : 1;

        std::array<std::uint32_t, PY_TENSOR_DIMS> shape_4d = {
            (uint32_t)batch_size,
            (uint32_t)queue_info.dim.t,
            (uint32_t)queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c * tt::constants::TILE_WIDTH,
            (uint32_t)queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r * tt::constants::TILE_HEIGHT};

        std::array<std::uint32_t, PY_TENSOR_DIMS> shape_3d = {
            1,
            (uint32_t)batch_size,
            (uint32_t)queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c * tt::constants::TILE_WIDTH,
            (uint32_t)queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r * tt::constants::TILE_HEIGHT};

        while (!tt::io::is_queue_empty(io_desc, queue_info, cluster)) {

            bool tensor_match = true;
            tt::tt_PytorchTensorDesc expected_expansion;

            // Peek using 4D dims
            {
                tt::tt_PytorchTensorDesc py_desc;

                auto get_status = tt::backend::get_output(io_desc, py_desc, !batched_push, 1);
                log_assert(get_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on get");
                log_assert(py_desc.shape == shape_4d, "Invalid shape");

                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                tt_tensor expected = expected_output.at(output_idx);
                tensor_match &= observed == expected;
                expected_expansion = py_desc;
            }

            // Manual expansion, fill in expected shape and strides
            int batch_dim = batched_push ? queue_info.input_count : 1;
            expected_expansion.shape[0] = 1;
            expected_expansion.shape[1] = batch_dim;
            expected_expansion.strides[0] = batch_dim * expected_expansion.strides[1];

            // Peek using 3D dims
            {
                tt::tt_PytorchTensorDesc py_desc;
                adapt_4d_tensor_to_3d_tensor(py_desc, true); // reduce dims from 4d to 3d

                auto get_status = tt::backend::get_output(io_desc, py_desc, !batched_push, 1);
                log_assert(get_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on get");

                tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push, false /*use_twos_complement*/, io_desc.input_count /*w_dim_override*/);
                tt_tensor expected = expected_output.at(output_idx);
                tensor_match &= observed == expected;

                log_assert(py_desc.shape == expected_expansion.shape, "Shape mismatch");
                log_assert(py_desc.strides == expected_expansion.strides, "Stride mismatch");
            }

            // Finally pop the output
            auto pop_status = tt::backend::pop_output(io_desc, !batched_push, 1);
            log_assert(pop_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on pop");

            pass &= tensor_match;
            log_info(tt::LogTest, "Output #{} {}", output_idx, tensor_match ? "PASSED" : "FAILED");
            output_idx++;
        }

        log_assert(expected_output.size() == output_idx, "Data mismatch");
    }

    // ----------------------------------------------------------------
    // PyBuda API for stopping backend
    // ----------------------------------------------------------------
    backend.finish();

    log_assert(pass, "Test failed");
    return 0;
}

