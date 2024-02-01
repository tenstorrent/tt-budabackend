// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct splice_config {
    int index = -1;
    int length = -1;
    int stride = -1;
};
struct tt_nary_config {
    NaryOp op = NaryOp::Invalid;
    int num_inputs = -1;
    tt::tt_grid_shape output_grid_shape = {0, 0};
    tt::tt_block_shape output_mblock_shape = {0, 0};
    tt::tt_block_shape output_ublock_shape = {0, 0};
    uint32_t batch_cnt;
    DataFormat out_data_format;
    Dim output_ublock_order = Dim::Invalid;
    SpliceMode splice_mode = SpliceMode::Invalid;
    std::vector<splice_config> splice_configs = {};
    std::vector<std::pair<int, bool>> kernel_broadcast = {};
};
class tt_nary_bare_op : public tt_op {
   public:
    // Model
    tt_nary_bare_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        NaryOp nary_op,
        std::uint32_t block_tile_dim,
        std::uint32_t block_cnt,
        std::uint32_t batch_cnt,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t gradient_op,
        bool untilize_output,
        MathFidelity math_fidelity,
        bool fp32_dest_acc_en,
        bool relu_en,
        std::uint32_t relu_threshold,
        ReluMode relu_mode,
        std::vector<DataFormat> in_data_formats,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        Dim output_ublock_order,
        SpliceMode splice_mode,
        std::vector<std::vector<int>> splice_configs,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        StochRndMode stoch_rnd_mode,
        const std::vector<std::vector<int>>& input_tile_dims,
        const std::vector<int>& output_tile_dims
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_nary_bare_op();

    //    vector<tt_pipe*>  connect_left_operand(tt_tensor *input_tensor, tt_buffer_grid *destination_buffer_grid, int
    //    per_block_m_tiles, int per_block_k_tiles);

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_nary_config m_config;
    std::uint32_t input_cnt;

    string get_hlks_file_name(NaryOp nary_op);

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
        std::uint32_t num_inputs,
        std::vector<splice_config> splice_configs,
        std::vector<DataFormat> in_data_formats,
        DataFormat out_data_format,
        std::vector<std::pair<int, bool>> kernel_broadcast);
};

namespace tt_nary::utils {
tt::tt_block_shape get_tile_indices_from_ublock_offsets(
    const tt::tt_block_shape &mblock,
    const tt::tt_block_shape &ublock,
    const Dim ublock_order,  // FIXME: Take a look here
    const int ublock_offset,
    const int tile_offset);
void ublock_splice(
    tt::tt_tensor &output_tensor, const std::vector<tt::tt_tensor> &inputs, const tt_nary_config &config);
void t_splice(tt::tt_tensor &output_tensor, const std::vector<tt::tt_tensor *> &inputs, const tt_nary_config &config);
void validate_input(const tt_nary_config &config, const tt_tensor &input, const int input_index);
string nary_op_to_string(NaryOp nary_op);
string splice_mode_to_string(SpliceMode splice_mode);
void golden_model(const tt_nary_config &config, vector<tt_tensor *> &inputs, tt_tensor *out);
}  // namespace tt_nary::utils
