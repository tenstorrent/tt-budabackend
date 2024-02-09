// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/base.hpp"
#include "device/tt_arch_types.h"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_tilizer_config {
    DataFormat out_data_format = {};
    tt_block_shape output_ublock_shape = {};
    tt_block_shape output_mblock_shape = {};
    unsigned int batch_cnt = 0;
    tt_grid_shape grid_shape = {};
    std::vector<int> input_tile_dims = {32, 32};
    std::vector<int> output_tile_dims = {32, 32};
};

class tt_tilizer_op : public tt_op {
   public:
    // Model
    tt_tilizer_op(
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
        bool untilize_output,
        MathFidelity math_fidelity,
        bool fp32_dest_acc_en,
        bool relu_en,
        std::uint32_t relu_threshold,
        ReluMode relu_mode,
        DataFormat in_0_data_format,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        std::uint32_t in_num_n_sub_blocks,
        std::uint32_t in_num_tiles_per_n_sub_block,
        const std::vector<int> &input_tile_dims,
        const std::vector<int> &out_tile_dims,
        const StochRndMode stoch_rnd_mode,
        tt::ARCH arch_name
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_tilizer_op();

    //    vector<tt_pipe*>  connect_left_operand(tt_tensor *input_tensor, tt_buffer_grid *destination_buffer_grid, int
    //    per_block_m_tiles, int per_block_k_tiles);

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_tilizer_config m_config;

    string get_hlks_file_name();

    //! Helper function to set the correct hlk_args with the proper value.
    void set_hlk_args_t(
        std::uint32_t block_tile_dim,
        std::uint32_t block_cnt,
        std::uint32_t batch_cnt,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        DataFormat in_0_data_format,
        DataFormat out_data_format,
        std::uint32_t in_num_n_sub_blocks,
        std::uint32_t in_num_tiles_per_n_sub_block);
};

namespace tt_tilizer::utils {
void golden_model(const tt_tilizer_config &config, vector<tt_tensor *> &inputs, tt_tensor *out);
}  // namespace tt_tilizer::utils
