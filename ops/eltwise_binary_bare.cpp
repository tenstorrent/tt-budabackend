// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "eltwise_binary_bare.hpp"

#include "common/model/tt_core.hpp"
#include "common/env_lib.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"
#include "model/model.hpp"

#include "hlks/inc/hlk_api.h"
#include "netlist_utils.hpp"
#include "tt_backend_api_types.hpp"

static constexpr int MAX_NUM_INPUTS = 2;
void tt_eltwise_binary_bare_op::set_hlk_args_t(
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    std::uint32_t transpose,
    DataFormat in_0_data_format,
    DataFormat in_1_data_format,
    DataFormat out_data_format,
    bool is_quantization_op,
    float zero_point,
    bool is_32bit_dest_en,
    std::vector<std::pair<int, bool>> kernel_broadcast) {
    if (m_config.op == BinaryOp::Invalid) {
        log_fatal("Unsupported eltwise binary op!");
    }

    log_assert(kernel_broadcast.size() <= MAX_NUM_INPUTS,
        "Passed kernel broadcast size {} is greater then the maximum number of supported inputs {}",
        kernel_broadcast.size(), MAX_NUM_INPUTS);

    tt::tt_hlk_args_desc hlk_args_descriptor;
    hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
    hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("gradient_op", gradient_op);
    hlk_args_descriptor.push_scalar_value("transpose", transpose);

    union {
        float f;
        std::uint32_t i;
    } f2i = {.f = zero_point};
    hlk_args_descriptor.push_scalar_value("zero_point", f2i.i);

    hlk_args_descriptor.push_scalar_value("is_32bit_dest_en", is_32bit_dest_en);

    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_eltwise_binary_bare_op::~tt_eltwise_binary_bare_op() {
}

tt_eltwise_binary_bare_op::tt_eltwise_binary_bare_op(
    string name,
    string type,
    tt_grid_shape grid_shape,
    tt_grid_shape grid_loc,
    bool grid_transpose,
    BinaryOp binary_op,
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
    DataFormat in_0_data_format,
    DataFormat in_1_data_format,
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    StochRndMode stoch_rnd_mode,
    float zero_point,
    const std::vector<std::vector<int>>& input_tile_dims,
    const std::vector<int>& output_tile_dims,
    TileDimReconfigMode is_tile_dim_reconfig_mode) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {
        .op = binary_op,
        .kernel_broadcast = kernel_broadcast,
        .transpose = transpose > 0,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims,
        .zero_point = zero_point
    };

    const bool is_quantization_op = binary_op == BinaryOp::Quantization || binary_op == BinaryOp::Dequantization ||
                                    binary_op == BinaryOp::Requantization;

    bool is_int32_add = false;
    if (binary_op == BinaryOp::Add) {
        bool has_one_int32 = in_0_data_format == DataFormat::Int32 || in_1_data_format == DataFormat::Int32;
        bool has_int_inputs = netlist_utils::is_int_format(in_0_data_format) && netlist_utils::is_int_format(in_1_data_format);
        is_int32_add = has_one_int32 && has_int_inputs;
    }

    bool int_fpu_en = netlist_utils::is_int_format(in_0_data_format) ||
                      netlist_utils::is_int_format(in_1_data_format) || netlist_utils::is_int_format(out_data_format);

    set_hlk_args_t(
        block_tile_dim,
        block_cnt,
        batch_cnt,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        gradient_op,
        transpose,
        in_0_data_format,
        in_1_data_format,
        out_data_format,
        is_quantization_op,
        zero_point,
        fp32_dest_acc_en || int_fpu_en, 
        kernel_broadcast);

    if (is_int32_add) {
        set_hlk_cpp_file_name_all_cores("hlks/binary/eltwise_binary_add_int32.cpp");
    }else if (binary_op == BinaryOp::Quantization) {
        set_hlk_cpp_file_name_all_cores("hlks/binary/eltwise_binary_quantization.cpp");
    } else if (binary_op == BinaryOp::Dequantization) {
        set_hlk_cpp_file_name_all_cores("hlks/binary/eltwise_binary_dequantization.cpp");
    } else if (binary_op == BinaryOp::Requantization) {
        set_hlk_cpp_file_name_all_cores("hlks/binary/eltwise_binary_requantization.cpp");
    } else if (binary_op == BinaryOp::Maximum) {
        set_hlk_cpp_file_name_all_cores("hlks/binary/eltwise_binary_maximum.cpp");
    } else {
        set_hlk_cpp_file_name_all_cores("hlks/binary/eltwise_binary_stream.cpp");
    }

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::in1, in_1_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);
    if (gradient_op) {
        set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, intermed_data_format);
    }

    set_hlk_math_fidelity_all_cores(math_fidelity);


    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->stoch_rnd_mode = stoch_rnd_mode;
    this->int_fpu_en = int_fpu_en;
    this->kernel_broadcasts = kernel_broadcast;
    for (int i=this->kernel_broadcasts.size(); i<MAX_NUM_INPUTS; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }

    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};

    for (int i = 0; i < input_tile_dims.size(); i++) {
        this->input_tile_dims.push_back({(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]});
    }
    this->is_tile_dim_reconfig_mode = is_tile_dim_reconfig_mode;
}

std::string tt_eltwise_binary_bare_op::type_to_str() {
    std::string ret = "tt_eltwise_binary_bare_op";
    return ret;
}

void tt_eltwise_binary_bare_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    tt_eltwise_binary::utils::golden_model(m_config, inputs, out);
}

string tt_eltwise_binary::utils::binary_op_to_string(BinaryOp binary_op) {
    switch (binary_op) {
        case BinaryOp::Add: return "add";
        case BinaryOp::Subtract: return "subtract";
        case BinaryOp::Multiply: return "multiply";
        case BinaryOp::Quantization: return "quantization";
        case BinaryOp::Dequantization: return "dequantization";
        case BinaryOp::Requantization: return "requantization";

        default: log_fatal("Unsupported eltwise binary op!");
    }
    return "";
}
void tt_eltwise_binary::utils::golden_model(
    const tt_eltwise_binary_config &config, vector<tt_tensor *> &inputs, tt_tensor *out) {
    log_assert(inputs.size() == 2, "Expected 2 inputs in binary op");
    log_assert(
        inputs[0]->same_shape(*inputs[1]),
        "inputs[0].shape={} must be same as inputs[1].shape={} in eltwise binary",
        inputs[0]->get_shape(),
        inputs[1]->get_shape());
    log_trace(tt::LogOp, "Eltwise Binary Op: {}", binary_op_to_string(config.op));

    log_assert(
        inputs[0]->get_data_format() != DataFormat::Invalid, "Input 0 data_format to tt_eltwise_binary is invalid");
    log_assert(
        inputs[1]->get_data_format() != DataFormat::Invalid, "Input 1 data_format to tt_eltwise_binary is invalid");
    if (config.transpose) {
        *inputs[0] = inputs[0]->transpose_xy(false, false, true);  // transpose tile
    }
    log_assert(out != nullptr, "Tensor output for eltwise binary golden model expected to be preallocated. Got a nullptr instead.");
    
    if (config.op == BinaryOp::Add) {
        tensor_lib::basic_ops::eltwise_binary(*out, *inputs[0], *inputs[1], tile_lib::binary::add);
    } else if (config.op == BinaryOp::Subtract) {
        tensor_lib::basic_ops::eltwise_binary(*out, *inputs[0], *inputs[1], tile_lib::binary::subtract);
    } else if (config.op == BinaryOp::Multiply) {
        tensor_lib::basic_ops::eltwise_binary(*out, *inputs[0], *inputs[1], tile_lib::binary::multiply);
    } else if (config.op == BinaryOp::Dequantization) {
        int input0_index = 0;
        int input1_index = 1;
        if (config.is_quantization_op_from_dest) {
            std::swap(input0_index, input1_index);
        }
        tt_tensor tmp(inputs[0]->get_shape(), inputs[1]->get_data_format());
        tmp.init_to_input_value(config.zero_point);
        tensor_lib::basic_ops::eltwise_binary(*out, *inputs[input0_index], tmp, tile_lib::binary::add);
        tensor_lib::basic_ops::eltwise_binary(*out, *out, *inputs[input1_index], tile_lib::binary::multiply);
    } else if (config.op == BinaryOp::Requantization || config.op == BinaryOp::Quantization) {
        tensor_lib::basic_ops::eltwise_binary(*out, *inputs[0], *inputs[1], tile_lib::binary::multiply);
        tt_tensor tmp(inputs[0]->get_shape(), out->get_data_format());
        tmp.init_to_input_value(config.zero_point);
        tensor_lib::basic_ops::eltwise_binary(*out, *out, tmp, tile_lib::binary::add);
    } else if (config.op == BinaryOp::Maximum) {
        tensor_lib::basic_ops::eltwise_binary(*out, *inputs[0], *inputs[1], tile_lib::binary::maximum);
    } else {
        log_fatal("Unsupported eltwise binary op!");
    }

    // Reduce output tensor tiles to specified tile dims.
    out->clear_tile_values(
        min(config.input_tile_dims[0][0], config.output_tile_dims[0]),
        min(config.input_tile_dims[0][1], config.output_tile_dims[1]));

    // Set output dims of tensor to the ones specified in netlist
    // Example:
    //   if input0 tile dims are 16x32, and input1 tile dims are 16x32, logical output tile dims will be 16x32, and other values will be zeroed (see above)
    //   But if specified output_tile_dims are, for example, 32x32, we need to set tensor tile height/width to
    //   those values since the rest of the stack expects so.
    out->metadata.shape.tile_height = config.output_tile_dims[0];
    out->metadata.shape.tile_width = config.output_tile_dims[1];

    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_eltwise_binary is invalid");
    log_assert(out->get_num_stored_tiles() > 0, "out get_num_stored_tiles to tt_eltwise_binary is empty");
}
