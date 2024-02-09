// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"
#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_matmul_config {
    std::uint32_t block_tile_dim = 0;
    std::uint32_t block_cnt = 0;
    std::uint32_t batch_cnt = 0;
    std::uint32_t num_m_sub_blocks = 0;
    std::uint32_t num_n_sub_blocks = 0;
    std::uint32_t num_tiles_per_m_sub_block = 0;
    std::uint32_t num_tiles_per_n_sub_block = 0;
    std::uint32_t transpose = 0;
    std::uint32_t gradient_op = 0;
    DataFormat out_data_format = DataFormat::Invalid;
    bool identity = false;
    std::uint32_t num_index_tiles = 0;
    std::uint32_t num_sparse_tiles = 0;
    std::uint32_t num_columns = 0;
    std::uint32_t sparse_tile_ptr_bits = 0;
    std::uint32_t sparse_ublock_idx_bits = 0;
    std::uint32_t block_tile_dim_bits = 0;
    std::uint32_t indices_len = 0;
    std::uint32_t fracture_factor = 0;
    std::uint32_t starting_row = 0;
    tt_grid_shape grid_shape = {};
    bool bias = false;
    bool accumulate = false;
    int z = 1;
    std::vector<std::pair<int, bool>> kernel_broadcast = {};
    SfpuOp sfpu_op = SfpuOp::Invalid;
    Dim sfpu_vector_mode = Dim::RC; // Does the whole tile by default
    bool requant = false;
    bool dequant = false;
    float zero_point = 0.0f;
    std::vector<std::vector<int>> input_tile_dims;
    std::vector<int> output_tile_dims;
    TileDimReconfigMode is_tile_dim_reconfig_mode = {0,0};
};
//! Matmul Op
class tt_mm_bare_op : public tt_op {
   public:
    // Model
    tt_mm_bare_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        std::uint32_t block_tile_dim,
        std::uint32_t block_cnt,
        std::uint32_t batch_cnt,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t gradient_op,
        std::uint32_t transpose,
        bool untilize_output,
        MathFidelity fidelity,
        bool fp32_dest_acc_en,
        bool relu_en,
        std::uint32_t relu_threshold,
        ReluMode relu_mode,
        std::vector<DataFormat>& in_data_format,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        bool identity,
        std::uint32_t num_index_tiles,
        std::uint32_t num_sparse_tiles,
        std::uint32_t num_columns,
        std::uint32_t sparse_tile_ptr_bits,
        std::uint32_t sparse_ublock_idx_bits,
        std::uint32_t block_tile_dim_bits,
        std::uint32_t indices_len,
        std::uint32_t fracture_factor,
        std::uint32_t starting_row, 
        bool bias,
        bool accumulate, 
        std::uint32_t z, 
        std::vector<bool> min_buffer_input,
        bool l1_acc_en, 
        std::vector<std::pair<int, bool>> kernel_broadcast,
        SfpuOp sfpu_op,
        Dim sfpu_vector_mode,
        SfpuExecutionThread sfpu_execution_thread,
        std::uint32_t act_t, 
        tt::ARCH arch_name,
        StochRndMode stoch_rnd_mode,
        bool requant,
        bool dequant,
        float zero_point,
        const std::vector<std::vector<int>>& input_tile_dims,
        const std::vector<int>& output_tile_dims,
        TileDimReconfigMode is_tile_dim_reconfig_mode
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_mm_bare_op();

    void model(vector<tt_tensor*>& inputs, tt_tensor* out) override;
    
   private:
    tt_matmul_config m_config;

    string get_hlks_file_name_no_extension(bool identity, bool is_int32_matmul);

    //! Helper function to set the correct hlk_args with the proper value.
    void set_hlk_args_t(
        std::uint32_t block_tile_dim,
        std::uint32_t block_cnt,
        std::uint32_t batch_cnt,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t transpose,
        std::uint32_t gradient_op,
        bool identity,
        std::uint32_t num_index_tiles,
        std::uint32_t num_sparse_tiles,
        std::uint32_t num_columns,
        std::uint32_t sparse_tile_ptr_bits,
        std::uint32_t sparse_ublock_idx_bits,
        std::uint32_t block_tile_dim_bits,
        std::uint32_t indices_len,
        std::uint32_t starting_row,
        std::uint32_t bias,
        bool accumulate, 
        std::uint32_t z,
        bool relu_en,
        std::uint32_t relu_threshold,
        ReluMode relu_mode,
        std::vector<bool> min_buffer_input,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        SfpuOp sfpu_op,
        Dim sfpu_vector_mode,
        bool l1_acc_en,
        bool shared_buffer,
        std::uint32_t act_t,
        bool adv_features_en,
        bool is_int32_matmul,
        bool requant,
        bool dequant,
        float zero_point
        );
};

namespace tt_matmul::utils {
//! Helper function for custom matmul_identity functionality --> mm_identity is closer to a sparse mm with
//! accumulation on z
void calculate_identity(
    const tt_matmul_config& config,
    tt_tensor* output,
    tt_tensor* activations,
    tt_tensor* sparse_input,
    tt_tensor* indexing_controls);
//! Helper function for custom matmul_identity functionality --> mm_identity is closer to a sparse mm with
//! accumulation on z -- This version does not have looping on the index tiles, so decoding is more straight forward
//! but risk not fitting
void calculate_identity_ljb(
    const tt_matmul_config& config,
    tt_tensor* output,
    tt_tensor* activations,
    tt_tensor* sparse_input,
    tt_tensor* indexing_controls);

void golden_model(const tt_matmul_config& config, vector<tt_tensor*>& inputs, tt_tensor* out);
}  // namespace tt_matmul::utils
