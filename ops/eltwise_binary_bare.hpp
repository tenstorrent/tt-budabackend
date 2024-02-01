// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_eltwise_binary_config {
    BinaryOp op = BinaryOp::Invalid;
    std::vector<std::pair<int, bool>> kernel_broadcast = {};
    bool transpose = false;
    std::vector<std::vector<int>> input_tile_dims;
    std::vector<int> output_tile_dims;
    float zero_point = 0.0f;
    bool is_quantization_op_from_dest = false;
};
class tt_eltwise_binary_bare_op : public tt_op {
   public:
    // Model
    tt_eltwise_binary_bare_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        BinaryOp binary_op,
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
        MathFidelity math_fidelity,
        bool fp32_dest_acc_en,
        bool relu_en,
        std::uint32_t relu_threshold,
        ReluMode relu_mode,
        DataFormat in_0_data_format,
        DataFormat in_1_data_format,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        StochRndMode stoch_rnd_mode,
        float zero_point,
        const std::vector<std::vector<int>>& input_tile_dims,
        const std::vector<int>& out_tile_dims,
        TileDimReconfigMode is_tile_dim_reconfig_mode
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_eltwise_binary_bare_op();

    //    vector<tt_pipe*>  connect_left_operand(tt_tensor *input_tensor, tt_buffer_grid *destination_buffer_grid, int
    //    per_block_m_tiles, int per_block_k_tiles);

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_eltwise_binary_config m_config;

    //! Helper function to set the correct hlk_args with the proper value.
    void set_hlk_args_t(
        std::uint32_t block_tile_dim,
        std::uint32_t block_cnt,
        std::uint32_t batch_cnt,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t gradient_op,
        std::uint32_t transpose,
        DataFormat in_0_data_format,
        DataFormat in_1_data_format,
        DataFormat out_data_format,
        bool is_quantization_op,
        float zero_point,
        bool is_32bit_dest_en,
        std::vector<std::pair<int, bool>> kernel_broadcast);
};

namespace tt_eltwise_binary::utils {
string binary_op_to_string(BinaryOp binary_op);
void golden_model(const tt_eltwise_binary_config &config, vector<tt_tensor *> &inputs, tt_tensor *out);
}  // namespace tt_eltwise_binary::utils
