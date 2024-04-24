// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "depthwise_op.hpp"
#include "utils/logger.hpp"

#include "common/model/tt_core.hpp"
#include "common/env_lib.hpp"
#include "netlist/netlist_utils.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"

#include "hlks/inc/depthwise_stream.h"

string tt_depthwise_op::get_hlks_file_name_no_extension(DepthwiseOp op)
{
    return "depthwise/depthwise" + tt_depthwise::utils::depthwise_op_to_string(op) + "_" + "stream";
}

void tt_depthwise_op::set_hlk_args_t(
    std::uint32_t batch_cnt,
    std::uint32_t block_tile_dim,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t block_cnt,    // number of blocks to accumulate - equal to number of rows in input1 matrix
    std::uint32_t gradient_op,
    std::uint32_t transpose,
    bool bias,
    bool relu_en,
    std::uint32_t relu_threshold,   // should this exist
    ReluMode relu_mode,             // should this exist
    std::vector<bool> min_buffer_input,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    std::uint32_t l1_acc_en,
    std::uint32_t shared_buffer,   // interm data format != output data format
    std::uint32_t adv_features_en) // arch specific control
{
    tt::tt_hlk_args_desc hlk_args_descriptor;
    int32_t relu_config = (int32_t)(relu_en ? (static_cast<uint32_t>(relu_en) | static_cast<uint32_t>(relu_mode) | (relu_threshold<<16)) : 0);

    if (m_config.op == DepthwiseOp::Matmul)
    {
        hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
        hlk_args_descriptor.push_scalar_value("block_tile_dim", block_tile_dim);
        hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
        hlk_args_descriptor.push_scalar_value("block_cnt", block_cnt);
        hlk_args_descriptor.push_scalar_value("gradient_op", gradient_op);
        hlk_args_descriptor.push_scalar_value("transpose", transpose);
        hlk_args_descriptor.push_scalar_value("bias", (int32_t)bias);
        hlk_args_descriptor.push_scalar_value("relu_config", relu_config);
        vector<int32_t> min_input_buffer = {(int32_t) min_buffer_input[0], (int32_t) min_buffer_input[1]};
        hlk_args_descriptor.push_1d_vector_values("min_input_buffer", {(int32_t) min_buffer_input[0], (int32_t) min_buffer_input[1]});

        hlk_args_descriptor.push_scalar_value("l1_acc_en", l1_acc_en);
        hlk_args_descriptor.push_scalar_value("shared_buffer", shared_buffer);
        hlk_args_descriptor.push_scalar_value("adv_features_en", adv_features_en);
    }
    else
    {
        log_fatal("Unsupported depthwise op!");
    }

    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_depthwise_op::~tt_depthwise_op() {}

tt_depthwise_op::tt_depthwise_op(
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
    DataFormat in_2_data_format,    // bias data format if exist
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    bool bias,
    bool l1_acc_en,
    std::vector<bool> min_buffer_input,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    tt::ARCH arch_name,
    StochRndMode stoch_rnd_mode,
    const std::vector<std::vector<int>> &input_tile_dims,
    const std::vector<int> &out_tile_dims
    ) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {
        .op = depthwise_op,
        .transpose = transpose > 0,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = out_tile_dims,
        .block_cnt = block_cnt,
        .bias = bias,
    };

    log_assert(min_buffer_input.size() == 2, "min_buffer_input={} for op={} must be sized 2", min_buffer_input, name);

    std::string root_dir = buda_home();

    bool int_fpu_en = netlist_utils::is_int_format(intermed_data_format) || netlist_utils::is_int_format(in_0_data_format) ||
                      netlist_utils::is_int_format(in_1_data_format) || netlist_utils::is_int_format(in_2_data_format);

    set_hlk_args_t(
        batch_cnt,
        block_tile_dim,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        block_cnt,
        gradient_op,
        transpose,
        bias,
        relu_en,
        relu_threshold,
        relu_mode,
        min_buffer_input,
        kernel_broadcast,
        l1_acc_en,
        (intermed_data_format == out_data_format),
        arch_name != tt::ARCH::GRAYSKULL);

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name_no_extension(m_config.op) + ".cpp");

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::in1, in_1_data_format);

    if (bias) {
        set_hlk_operand_dataformat_all_cores(HlkOperand::in2, in_2_data_format);
    }
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);

    set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, intermed_data_format);

    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->int_fpu_en = int_fpu_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->kernel_broadcasts = kernel_broadcast;
    for (int i=this->kernel_broadcasts.size(); i < DEPTHWISE_MAX_NUM_INPUTS; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }

    for (int i = 0; i < input_tile_dims.size(); i++) {
        this->input_tile_dims.push_back({(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]});
    }
}

std::string tt_depthwise_op::type_to_str() {
    std::string ret = "tt_depthwise_op";
    return ret;
}

void tt_depthwise_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    tt_depthwise::utils::golden_model(m_config, inputs, out);
}

string tt_depthwise::utils::depthwise_op_to_string(DepthwiseOp op) {
    switch (op) {
        case DepthwiseOp::Matmul:
            return "";
        default: log_fatal("Unsupported depthwise op!");
    }
    return "";
}

void tt_depthwise::utils::golden_model(const tt_depthwise_config &config, vector<tt_tensor *> &inputs, tt_tensor *out) {
    log_assert(inputs.size() == (config.bias ? 3 : 2), "Invalid number of inputs for depthwise op!");
    if (config.transpose) {
        *inputs[1] = inputs[1]->transpose_xy(false, false, true);
    }

    tt::tt_shape shape = inputs[0]->get_shape();
    shape.ct = shape.ct / config.block_cnt;
    *out = tt_tensor(shape, inputs[0]->get_data_format());

    if (config.op == DepthwiseOp::Matmul) {
        // tt_tensor tmp_out = tt_tensor(out->get_shape(), out->get_data_format());
        auto fn = [](tt::tt_tile& out, const tt::tt_tile& in0,  const tt::tt_tile& in1) {
            out += in0.matmul(in1);
        };

        tensor_lib::basic_ops::depthwise_binary(*out, *inputs[0], *inputs[1], config.block_cnt, fn);

        if (config.bias) {
            *out = out->add(inputs[2]->broadcast_within_tiles(Dim::R, false));
        }

    } else {
        log_fatal("Unsupported depthwise op!");
    }

    out->clear_tile_values(config.input_tile_dims[0][0], config.input_tile_dims[0][1]);

    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_depthwise_op is invalid");
    log_assert(out->get_num_stored_tiles() > 0, "out get_num_stored_tiles to tt_depthwise_op is empty");
}