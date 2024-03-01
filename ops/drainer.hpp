// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_drainer_config {
    DrainerOp op = DrainerOp::Invalid;
    std::uint32_t transpose = false;
    std::vector<std::pair<int, bool>> kernel_broadcast;
    std::vector<int> input_tile_dims = {32, 32};
    std::vector<int> output_tile_dims = {32, 32};
};

class tt_drainer_op : public tt_op {
   public:
    // Model
    tt_drainer_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        DrainerOp drainer_op,
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
        std::vector<DataFormat> in_data_formats,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        const std::vector<int> &input_tile_dims,
        const std::vector<int> &output_tile_dims,
        const StochRndMode stoch_rnd_mode
    );

    ~tt_drainer_op();

    //    vector<tt_pipe*>  connect_left_operand(tt_tensor *input_tensor, tt_buffer_grid *destination_buffer_grid, int
    //    per_block_m_tiles, int per_block_k_tiles);

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_drainer_config m_config;
    std::uint32_t input_cnt;

    string get_hlks_file_name(DrainerOp drainer_op);

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
        std::vector<std::pair<int, bool>> kernel_broadcast);
};

namespace tt_drainer::utils {
string drainer_op_to_string(DrainerOp drainer_op);
void golden_model(const tt_drainer_config &config, vector<tt_tensor *> &inputs, tt_tensor *out);
}  // namespace tt_drainer::utils
