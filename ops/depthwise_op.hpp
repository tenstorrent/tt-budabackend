// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_depthwise_config {
    DepthwiseOp op = DepthwiseOp::Invalid;
    std::uint32_t transpose = false;
    std::vector<std::vector<int>> input_tile_dims;
    std::vector<int> output_tile_dims;
    std::uint32_t block_cnt = 1;
    bool bias = false;
};

class tt_depthwise_op : public tt_op {
   public:
    // Model
    tt_depthwise_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        DepthwiseOp depthwise_op,
        std::uint32_t batch_cnt,
        std::uint32_t block_tile_dim,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t block_cnt,
        std::uint32_t gradient_op,   // should this exist
        std::uint32_t transpose,
        bool untilize_output,           // should this exist
        MathFidelity math_fidelity,
        bool fp32_dest_acc_en,
        bool relu_en,                   // should this exist
        std::uint32_t relu_threshold,   // should this exist
        ReluMode relu_mode,             // should this exist
        DataFormat in_0_data_format,
        DataFormat in_1_data_format,
        DataFormat in_2_data_format,     // bias data foramt
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        bool bias,                       // has input 2 (bias)
        bool l1_acc_en,
        std::vector<bool> min_buffer_input,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        tt::ARCH arch_name,
        StochRndMode stoch_rnd_mode,
        const std::vector<std::vector<int>> &input_tile_dims,
        const std::vector<int> &out_tile_dims
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_depthwise_op();

    //    vector<tt_pipe*>  connect_left_operand(tt_tensor *input_tensor, tt_buffer_grid *destination_buffer_grid, int
    //    per_block_m_tiles, int per_block_k_tiles);

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_depthwise_config m_config;

    string get_hlks_file_name_no_extension(DepthwiseOp depthwise_op);

    //! Helper function to set the correct hlk_args with the proper value.
    void set_hlk_args_t(
        std::uint32_t batch_cnt,
        std::uint32_t block_tile_dim,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t block_cnt,    // number of blocks to accumulate - equal to number of rows in input1 matrix
        std::uint32_t gradient_op,
        std::uint32_t transpose,
        bool          bias,
        bool          relu_en,
        std::uint32_t relu_threshold,   // should this exist
        ReluMode      relu_mode,             // should this exist
        std::vector<bool> min_buffer_input,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        std::uint32_t l1_acc_en,
        std::uint32_t shared_buffer,   // interm data format != output data format
        std::uint32_t adv_features_en); // arch specific control
};

namespace tt_depthwise::utils {
    string depthwise_op_to_string(DepthwiseOp depthwise_op);
    void golden_model(const tt_depthwise_config &config, vector<tt_tensor *> &inputs, tt_tensor *out);
}  // namespace tt_tilizer::utils
