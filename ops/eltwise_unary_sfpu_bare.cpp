// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "eltwise_unary_sfpu_bare.hpp"

#include "common/model/tt_core.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"
#include "model/model.hpp"
#include "utils/logger.hpp"

tt_eltwise_unary_sfpu_bare_op::~tt_eltwise_unary_sfpu_bare_op() = default;

static constexpr int MAX_NUM_INPUTS = 1;

void tt_eltwise_unary_sfpu_bare_op::set_hlk_args_t(
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
    std::vector<std::pair<int, bool>> kernel_broadcast) {

    // These are not specifyable in a netlist
    if (m_config.op == SfpuOp::Invalid || m_config.op == SfpuOp::Max || m_config.op == SfpuOp::CastFp32ToFp16a || m_config.op == SfpuOp::ReluMax || m_config.op == SfpuOp::ReluMin) {
        log_assert(false, "Unsupported SfpuOp");
        return;
    }

    log_assert(
        kernel_broadcast.size() <= MAX_NUM_INPUTS,
        "Passed kernel broadcast size {} is greater then the maximum number of supported inputs {}",
        kernel_broadcast.size(),
        MAX_NUM_INPUTS);

    tt::tt_hlk_args_desc hlk_args_descriptor;
    hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
    hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("vector_mode", (int32_t)vector_mode);
    hlk_args_descriptor.push_scalar_value("gradient_op", 0);  // unused, set to zero
    hlk_args_descriptor.push_scalar_value("transpose", transpose);

    // Dropout specific
    float tmp_p = probability * (float)std::numeric_limits<std::uint16_t>::max();
    hlk_args_descriptor.push_scalar_value("probability", (int32_t)tmp_p);
    hlk_args_descriptor.push_scalar_value("scale", scale_u16b);
    hlk_args_descriptor.push_scalar_value("seed", seed);

    // Lrelu specific
    hlk_args_descriptor.push_scalar_value("slope", slope_u16b);

    // Power specific
    hlk_args_descriptor.push_scalar_value("exponent", exponent);

    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_eltwise_unary_sfpu_bare_op::tt_eltwise_unary_sfpu_bare_op(
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
    const StochRndMode stoch_rnd_mode)
        : tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {
        .op = sfpu_op,
        .approximation_mode = approximation_mode,
        .transpose = transpose,
        .probability = probability,
        .scale = scale,
        .exponent = exponent,
        .slope = slope,
        .vector_mode = (input_tile_dims[0] <= 16 || input_tile_dims[1] <= 16) ? Dim::RC : vector_mode, // For tiles with <=16 rows or <= 16 cols override vector mode to RC
        .kernel_broadcast = kernel_broadcast, 
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims,
    };
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
        probability,
        scale_u16b,
        seed,
        exponent,
        slope_u16b,
        in_data_format,
        out_data_format,
        vector_mode,
        kernel_broadcast);
        
    set_hlk_cpp_file_name_all_cores("hlks/sfpu/eltwise_unary_sfpu_stream.cpp");

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);
    if (gradient_op) {
        set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, intermed_data_format);
    }

    set_hlk_math_fidelity_all_cores(math_fidelity);

    set_hlk_math_approx_mode_all_cores(approximation_mode);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->sfpu_execution_thread = sfpu_execution_thread;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->input_tile_dims.push_back({(uint32_t)input_tile_dims[0], (uint32_t)input_tile_dims[1]});
    this->stoch_rnd_mode = stoch_rnd_mode;
    this->kernel_broadcasts = kernel_broadcast;
    for (int i=this->kernel_broadcasts.size(); i<MAX_NUM_INPUTS; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }
}

void tt_eltwise_unary_sfpu_bare_op::model(vector<tt_tensor*>& inputs, tt_tensor* out) {
    tt_eltwise_unary::utils::golden_model(m_config, inputs, out);
}

string tt_eltwise_unary::utils::sfpu_op_to_string(SfpuOp sfpu_op) {
    switch (sfpu_op) {
        case SfpuOp::Exp: return "sfpu_exp";
        case SfpuOp::Log: return "sfpu_log";
        case SfpuOp::Sigmoid: return "sfpu_sigmoid";
        case SfpuOp::Sqrt: return "sfpu_sqrt";
        case SfpuOp::Gelu: return "sfpu_gelu";
        case SfpuOp::GeluDerivative: return "sfpu_gelu_derivative";
        case SfpuOp::Reciprocal: return "sfpu_reciprocal";
        case SfpuOp::Tanh: return "sfpu_tanh";
        case SfpuOp::Dropout: return "sfpu_dropout";
        case SfpuOp::Square: return "sfpu_square";
        case SfpuOp::Power: return "sfpu_power";
        case SfpuOp::Sine: return "sfpu_sine";
        case SfpuOp::Cosine: return "sfpu_cosine";
        case SfpuOp::LRelu: return "sfpu_lrelu";
        case SfpuOp::Abs: return "sfpu_abs";
        default: log_assert(false, "Unrecognized SfpuOp supported");
    }
    return "";
}

void tt_eltwise_unary::utils::golden_model(
    const tt_eltwise_unary_config& config, vector<tt_tensor*>& inputs, tt_tensor* out) {
    log_assert(inputs.size() == 1, "Incorrect input count for eltwise unary.");
    log_debug(tt::LogOp, "Golden -- Sfpu Op: {}", sfpu_op_to_string(config.op));

    log_assert(
        inputs[0]->get_data_format() != DataFormat::Invalid, "Input 0 data_format to tt_eltwise_unary is invalid");

    if (config.transpose) {
        *inputs[0] = inputs[0]->transpose_xy(false, false, true);  // transpose tile
    }

    // Try to use "eltwise_unary" as much as possible, except cases where we need more params or inputs, then
    // implement and use a custom function
    switch (config.op) {
        case SfpuOp::Exp:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::exp);
            break;
        case SfpuOp::Log:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::log);
            break;
        case SfpuOp::Sigmoid:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::sigmoid);
            break;
        case SfpuOp::Sqrt:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::sqrt);
            break;
        case SfpuOp::Gelu:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::gelu);
            break;
        case SfpuOp::GeluDerivative:
            tensor_lib::basic_ops::eltwise_unary(
                *out, *inputs[0], config.vector_mode, tile_lib::unary::gelu_derivative);
            break;
        case SfpuOp::Reciprocal:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::reciprocal);
            break;
        case SfpuOp::Tanh:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::tanh);
            break;
        case SfpuOp::Square:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::square);
            break;
        case SfpuOp::Power: tensor_lib::power(*out, *inputs[0], config.vector_mode, config.exponent); break;
        case SfpuOp::Dropout:
            tensor_lib::dropout(*out, *inputs[0], config.vector_mode, config.probability, config.scale);
            break;
        case SfpuOp::Sine:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::sine);
            break;
        case SfpuOp::Cosine:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::cosine);
            break;
        case SfpuOp::LRelu:
            tensor_lib::lrelu(*out, *inputs[0], config.vector_mode, config.slope);
            break;
        case SfpuOp::Abs:
            tensor_lib::basic_ops::eltwise_unary(*out, *inputs[0], config.vector_mode, tile_lib::unary::abs);
            break;
        default: log_assert(false, "Unrecognized SfpuOp"); break;
    }

    // Reduce output tensor tiles to specified tile dims.
    out->clear_tile_values(
        min(config.input_tile_dims[0], config.output_tile_dims[0]),
        min(config.input_tile_dims[1], config.output_tile_dims[1]));

    // Set output dims of tensor to the ones specified in netlist
    // Example:
    //   If this op has inputs 1x32, only first row of the output will be non-zero values (see comment above)
    //   But if specified output_tile_dims are, for example, 32x32, we need to set tensor tile height/width to
    //   those values since the rest of the stack expects so.
    out->set_tile_dims(config.output_tile_dims[0], config.output_tile_dims[1]);

    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_eltwise_unary is invalid");
    log_assert(out->get_num_stored_tiles() > 0, "out get_num_stored_tiles to tt_eltwise_unary is empty");
}
