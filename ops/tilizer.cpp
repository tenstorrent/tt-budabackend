// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tilizer.hpp"

#include "common/model/tt_core.hpp"
#include "common/env_lib.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"
#include "device/tt_arch_types.h"

string tt_tilizer_op::get_hlks_file_name() { return "tilizer/tilizer_stream.cpp"; }

void tt_tilizer_op::set_hlk_args_t(
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
    std::uint32_t in_num_tiles_per_n_sub_block) {

    tt::tt_hlk_args_desc hlk_args_descriptor;
    hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks * batch_cnt);
    hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
    hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("in_num_n_sub_blocks", in_num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("in_num_tiles_per_n_sub_block", in_num_tiles_per_n_sub_block);

    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_tilizer_op::~tt_tilizer_op() {}

tt_tilizer_op::tt_tilizer_op(
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
    bool untilizer_output,
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
    const std::vector<int> &output_tile_dims,
    const StochRndMode stoch_rnd_mode,
    tt::ARCH arch_name
    ) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {
        .out_data_format = out_data_format,
        .output_ublock_shape =
            {
                .r = num_tiles_per_m_sub_block,
                .c = num_tiles_per_n_sub_block,
            },
        .output_mblock_shape =
            {
                .r = num_m_sub_blocks,
                .c = num_n_sub_blocks,
            },
        .batch_cnt = batch_cnt,
        .grid_shape = grid_shape,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims,
    };
    std::string root_dir = buda_home();

    set_hlk_args_t(
        block_tile_dim,
        block_cnt,
        batch_cnt,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        in_0_data_format,
        out_data_format,
        in_num_n_sub_blocks,
        in_num_tiles_per_n_sub_block);

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name());

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);

    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilizer_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->input_tile_dims.push_back({(uint32_t)input_tile_dims[0], (uint32_t)input_tile_dims[1]});
    this->stoch_rnd_mode = stoch_rnd_mode;

    if (arch_name == tt::ARCH::BLACKHOLE) {
        this->tilize_input = true;
    }
}

std::string tt_tilizer_op::type_to_str() {
    std::string ret = "tt_tilizer";
    return ret;
}

void tt_tilizer_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    tt_tilizer::utils::golden_model(m_config, inputs, out);
}
void tt_tilizer::utils::golden_model(const tt_tilizer_config &config, vector<tt_tensor *> &inputs, tt_tensor *out) {
    log_assert(inputs.size() == 1, "tilizer must have one inputs");

    // Reduce input tensor tiles to their tile dims.
    inputs[0]->clear_tile_values(config.input_tile_dims[0], config.input_tile_dims[1]);
    inputs[0]->set_is_tilized(false);

    *out = *inputs[0];
    out->tilize_inplace(true, false);
    out->set_is_tilized(true);
    
    // Reduce output tensor tiles to specified tile dims.
    out->clear_tile_values(config.output_tile_dims[0], config.output_tile_dims[1]);

    // Set output dims of tensor to the ones specified in netlist
    // Example:
    //   If this op has inputs 1x32, only first row of the output will be non-zero values (see comment above)
    //   But if specified output_tile_dims are, for example, 32x32, we need to set tensor tile height/width to
    //   those values since the rest of the stack expects so. 
    out->metadata.shape.tile_height = config.output_tile_dims[0];
    out->metadata.shape.tile_width = config.output_tile_dims[1];

    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_tilizer is invalid");
    log_trace(tt::LogOp, "tilizer out dim {}", out->get_shape());
}

