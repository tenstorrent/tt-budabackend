// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_eltwise_unary_config {
    SfpuOp op = SfpuOp::Invalid;
    bool approximation_mode = false;
    std::uint32_t transpose = false;
    float probability = 0;
    float scale = 0;
    std::int32_t exponent = 0;
    float slope = 0;
    Dim vector_mode = Dim::RC; // Does the whole tile by default
    std::vector<std::pair<int, bool>> kernel_broadcast =  {};
    std::vector<int> input_tile_dims = {32, 32};
    std::vector<int> output_tile_dims = {32, 32};
};
//! Eltwise Unary Op
class tt_eltwise_unary_sfpu_bare_op : public tt_op {
   public:
    tt_eltwise_unary_sfpu_bare_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        SfpuOp sfpu_op,
        bool approximation_mode,
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
        float probability,
        float scale,
        std::uint32_t scale_u16b,
        std::uint32_t seed,
        std::int32_t exponent,
        float slope, 
        std::uint32_t slope_u16b, 
        DataFormat in_data_format,
        DataFormat out_data_format,
        DataFormat intermed_data_format, 
        Dim vector_mode,
        SfpuExecutionThread sfpu_execution_thread, 
        std::vector<std::pair<int, bool>> kernel_broadcast,
        const std::vector<int>& input_tile_dims,
        const std::vector<int>& output_tile_dims,
        const StochRndMode stoch_rnd_mode);
    ~tt_eltwise_unary_sfpu_bare_op();

    //! Model Implementation
    void model(vector<tt_tensor*>& inputs, tt_tensor* out) override;

   private:
    tt_eltwise_unary_config m_config = {};

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
        float probability,
        std::uint32_t scale_u16b,
        std::uint32_t seed,
        std::int32_t exponent,
        std::uint32_t slope_u16b, 
        DataFormat in_data_format,
        DataFormat out_data_format,
        Dim vector_mode, 
        std::vector<std::pair<int, bool>> kernel_broadcast);
};

namespace tt_eltwise_unary::utils {
string sfpu_op_to_string(SfpuOp sfpu_op);
void golden_model(const tt_eltwise_unary_config& config, vector<tt_tensor*>& inputs, tt_tensor* out);
}  // namespace tt_eltwise_unary::utils
