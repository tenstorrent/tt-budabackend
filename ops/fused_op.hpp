// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

#include "common/base.hpp"
#include "netlist/netlist_fused_op_info_types.hpp"
#include "netlist/netlist_op_info_types.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

class tt_fused_op : public tt_op {
   public:
    // Model
    tt_fused_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        bool approximation_mode,
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
        string id,
        std::vector<DataFormat> input_data_formats,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        std::vector<std::pair<int, bool>> kernel_broadcast,
        const tt_fused_op_info &fused_op_info,
        const std::vector<tt_op_info> &scheduled_op_infos,
        const std::vector<vector<int>> &input_tile_dims,
        const std::vector<int> &output_tile_dims,
        const StochRndMode stoch_rnd_mode
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_fused_op();

    //    vector<tt_pipe*>  connect_left_operand(tt_tensor *input_tensor, tt_buffer_grid *destination_buffer_grid, int
    //    per_block_m_tiles, int per_block_k_tiles);

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    std::uint32_t input_cnt = 2;
    std::uint32_t output_cnt = 1;
    std::uint32_t intermed_cnt = 1;
    tt_fused_op_info m_fused_op_info;
    std::vector<tt_op_info> m_scheduled_op_infos = {};

    string get_hlks_file_name(string id);


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
        std::vector<std::pair<int, bool>> kernel_broadcast
        );
};
