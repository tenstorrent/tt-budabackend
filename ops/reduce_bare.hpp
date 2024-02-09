// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "common/base.hpp"
#include "model/op.hpp"
#include "model/tensor.hpp"

struct tt_reduce_config {
    ReduceFunc reduce_func = ReduceFunc::Invalid;
    Dim reduce_dim = Dim::Invalid;
    std::uint32_t z_dim = 0;
};
//! Reduce Op
class tt_reduce_bare_op : public tt_op {
   public:
    tt_reduce_bare_op(
        string name,
        string type,
        tt_grid_shape grid_shape,
        tt_grid_shape grid_loc,
        bool grid_transpose,
        ReduceFunc reduce_func,
        Dim reduce_dim,
        std::uint32_t block_tile_dim,
        std::uint32_t block_cnt,
        std::uint32_t batch_cnt,
        std::uint32_t num_m_sub_blocks,
        std::uint32_t num_n_sub_blocks,
        std::uint32_t num_tiles_per_m_sub_block,
        std::uint32_t num_tiles_per_n_sub_block,
        std::uint32_t gradient_op,
        std::uint32_t transpose,
        std::uint32_t z_dim,
        bool untilize_output,
        MathFidelity math_fidelity,
        bool fp32_dest_acc_en,
        bool relu_en,
        std::uint32_t relu_threshold,
        ReluMode relu_mode,
        DataFormat in_data_format,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        const std::vector<int>& input_tile_dims,
        const std::vector<int>& output_tile_dims,
        const StochRndMode stoch_rnd_mode);
    ~tt_reduce_bare_op();

    static std::string type_to_str();

    //! Model Implementation
    void model(vector<tt_tensor*>& inputs, tt_tensor* out) override;

   private:
    tt_reduce_config m_config;

    string get_hlks_file_name(ReduceFunc reduce_func, Dim reduce_dim);

    DstSize get_dst_size() const override;

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
        std::uint32_t z_dim,
        DataFormat in_data_format,
        DataFormat out_data_format,
        DataFormat intermed_data_format,
        bool is_32bit_dest_en);
};

namespace tt_reduce::utils {
string reduce_func_to_string(ReduceFunc reduce_func);
string reduce_dim_to_string(Dim reduce_dim);
void golden_model(const tt_reduce_config& config, vector<tt_tensor*>& inputs, tt_tensor* out);
}  // namespace tt_reduce::utils
