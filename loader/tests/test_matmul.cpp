// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "compile_trisc.hpp"
#include "runtime.hpp"
#include "tt_backend.hpp"
#include "verif.hpp"

tt_tensor get_tilized_tensor(uint32_t rdim, uint32_t cdim) {
    log_assert(rdim % tt::constants::TILE_HEIGHT == 0, "Invalid rdim");
    log_assert(cdim % tt::constants::TILE_WIDTH == 0, "Invalid cdim");

    tt_tensor_metadata md;
    md.shape.rt = rdim / tt::constants::TILE_HEIGHT;
    md.shape.ct = cdim / tt::constants::TILE_WIDTH;
    md.shape.z  = 1;
    md.shape.w  = 1;
    md.is_tilized = true;
    md.data_format = DataFormat::Float16_b;

    tt_tensor tensor(md);
    tensor.init_to_xyz_values();
    return tensor;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    int seed;
    int sweep_steps;
    uint32_t M, K, N;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(seed, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", 0);
    std::tie(M, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--M", 32);
    std::tie(K, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--K", 32);
    std::tie(N, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--N", 32);
    std::tie(sweep_steps, args)  = verif_args::get_command_option_uint32_and_remaining_args(args, "--sweep-steps", 1);

    std::vector<uint32_t> M_vals = {M};
    std::vector<uint32_t> K_vals = {K};
    std::vector<uint32_t> N_vals = {N};

    if (sweep_steps > 1) {
        for (int step = 0; step < sweep_steps; step++) {
            M_vals.push_back(M_vals.back()*2);
            K_vals.push_back(K_vals.back()*2);
            N_vals.push_back(N_vals.back()*2);
        }
    }

    std::vector<tt_tensor> results_slow;
    std::vector<tt_tensor> results_fast;

    for (int i = 0; i < sweep_steps; i++) {
        auto A = get_tilized_tensor(M_vals.at(i), K_vals.at(i));
        auto B = get_tilized_tensor(K_vals.at(i), N_vals.at(i));

        auto tensor_op_start = std::chrono::high_resolution_clock::now();
        auto C = A.matmul(B, false, false);
        auto tensor_op_end = std::chrono::high_resolution_clock::now();
        auto tensor_op_duration = std::chrono::duration_cast<std::chrono::microseconds>(tensor_op_end - tensor_op_start).count();

        results_slow.push_back(C);
        log_info(tt::LogTest, "MatMulBAD {}x{} * {}x{} operation duration = {} ms", M_vals.at(i), K_vals.at(i), K_vals.at(i), N_vals.at(i), (float)tensor_op_duration/1000);
    }

    for (int i = 0; i < sweep_steps; i++) {
        auto A = get_tilized_tensor(M_vals.at(i), K_vals.at(i));
        auto B = get_tilized_tensor(K_vals.at(i), N_vals.at(i));

        auto tensor_op_start = std::chrono::high_resolution_clock::now();
        auto C = A.matmul(B, false, true);
        auto tensor_op_end = std::chrono::high_resolution_clock::now();
        auto tensor_op_duration = std::chrono::duration_cast<std::chrono::microseconds>(tensor_op_end - tensor_op_start).count();

        results_fast.push_back(C);
        log_info(tt::LogTest, "MatMulAVX {}x{} * {}x{} operation duration = {} ms", M_vals.at(i), K_vals.at(i), K_vals.at(i), N_vals.at(i), (float)tensor_op_duration/1000);
    }

    for (int i = 0; i < sweep_steps; i++) {
        if (results_slow.at(i) != results_fast.at(i)) {
            log_error("LHS vs. RHS mismatch:");
            results_slow.at(i).dump();
            results_fast.at(i).dump();
        };
    }
    return 0;
}

