// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "common/base.hpp"
#include "model/op.hpp"
#include "netlist_op_info_types.hpp"

namespace tt {
    class tt_tensor;
};

struct tt_topk_config {
    vector<std::pair<int, bool>> kernel_broadcast;
    std::vector<std::vector<int>> input_tile_dims;
    std::vector<int> output_tile_dims;
    int k = 0;
    TopKSort sort = TopKSort::None;
    bool kreduce = false;
};
class tt_topk_op : public tt_op {
   public:
    // Model
    tt_topk_op(
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
        DataFormat in_0_data_format,
        DataFormat in_1_data_format,
        DataFormat out_data_format,
        const vector<std::pair<int, bool>>& kernel_broadcast,
        int k,
        TopKSort sort,
        bool kreduce,
        const std::vector<std::vector<int>>& input_tile_dims,
        const std::vector<int>& out_tile_dims
        );

    // Return output shape based on the input shape

    static std::string type_to_str();

    ~tt_topk_op() = default;

    void model(vector<tt_tensor *> &inputs, tt_tensor *out) override;

   private:
    tt_topk_config config;

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
        DataFormat in_1_data_format,
        DataFormat out_data_format,
        std::uint32_t k,
        std::uint32_t sort);
};