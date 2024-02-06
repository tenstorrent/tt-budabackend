// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "reduce_bare.hpp"

#include "common/model/tt_core.hpp"
#include "common/param_lib.hpp"
#include "netlist/netlist_utils.hpp"
#include "model/model.hpp"
#include "utils/logger.hpp"

#include "hlks/inc/reduce_common.h"

string tt_reduce_bare_op::get_hlks_file_name(ReduceFunc reduce_func, Dim reduce_dim) {
    return "reduce/reduce_" + tt_reduce::utils::reduce_dim_to_string(reduce_dim) + "_" +
           tt_reduce::utils::reduce_func_to_string(reduce_func) + "_" + "stream" + ".cpp";
}

tt_reduce_bare_op::~tt_reduce_bare_op() {
}

DstSize tt_reduce_bare_op::get_dst_size() const {
    if (m_config.reduce_func == ReduceFunc::Max && m_config.reduce_dim == Dim::RC) {
        return DstSize::FullSize;
    }
    return DstSize::HalfSize;
}

void tt_reduce_bare_op::set_hlk_args_t(
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
    bool is_32bit_dest_en) {
    // set-up HLK descriptor
    if (m_config.reduce_func == ReduceFunc::Max) {
        if (m_config.reduce_dim == Dim::C || m_config.reduce_dim == Dim::R || m_config.reduce_dim == Dim::RC ||
            m_config.reduce_dim == Dim::Z) {
            tt::tt_hlk_args_desc hlk_args_descriptor;

            int32_t reduce_specific_block_tile_dim =
                m_config.reduce_dim == Dim::Z ? num_tiles_per_m_sub_block * num_tiles_per_n_sub_block : block_tile_dim;

            int32_t reduce_specific_block_cnt =
                m_config.reduce_dim == Dim::Z ? num_m_sub_blocks * num_n_sub_blocks : block_cnt;

            hlk_args_descriptor.push_scalar_value("block_tile_dim", reduce_specific_block_tile_dim);
            hlk_args_descriptor.push_scalar_value("block_cnt", reduce_specific_block_cnt);
            hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
            hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
            hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
            hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
            hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
            hlk_args_descriptor.push_scalar_value("gradient_op", 0);  // unused, set to zero
            hlk_args_descriptor.push_scalar_value("transpose", 0);    // unused, set to zero

            // Reduce Z specific
            hlk_args_descriptor.push_scalar_value("z_dim", z_dim);
            hlk_args_descriptor.push_scalar_value("is_32bit_dest_en", is_32bit_dest_en);

            cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
        } else {
            log_assert(false, "Unsupported ReduceDim");
        }
    } else {
        log_assert(false, "Unsupported ReduceFunc");
    }
}
/// FIXME: Re-evaluate consolidation of hlks to one single file --> Would really clean up the op from a code perspective

tt_reduce_bare_op::tt_reduce_bare_op(
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
    const StochRndMode stoch_rnd_mode) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {.reduce_func = reduce_func, .reduce_dim = reduce_dim, .z_dim = z_dim};
    std::string root_dir = buda_home();

    bool int_fpu_en = netlist_utils::is_int_format(intermed_data_format) || netlist_utils::is_int_format(in_data_format);

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
        z_dim,
        in_data_format,
        out_data_format,
        intermed_data_format,
        fp32_dest_acc_en || int_fpu_en);

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name(reduce_func, reduce_dim));

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);
    if ((gradient_op) || (reduce_dim == Dim::Z)) {
        set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, intermed_data_format);
    }

    set_hlk_math_fidelity_all_cores(math_fidelity);


    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->int_fpu_en = int_fpu_en;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->input_tile_dims.push_back({(uint32_t)input_tile_dims[0], (uint32_t)input_tile_dims[1]});
    this->stoch_rnd_mode = stoch_rnd_mode;
}

std::string tt_reduce_bare_op::type_to_str() { return "tt_reduce_bare_op"; }

void tt_reduce_bare_op::model(vector<tt_tensor*>& inputs, tt_tensor* out) {
    tt_reduce::utils::golden_model(m_config, inputs, out);
}

string tt_reduce::utils::reduce_func_to_string(ReduceFunc reduce_func) {
    switch (reduce_func) {
        case ReduceFunc::Avg: return "avg";
        case ReduceFunc::Max: return "max";
        case ReduceFunc::Sum: return "sum";
        default: log_assert(false, "Unrecognized ReduceFunc supported");
    }
    return "";
}
string tt_reduce::utils::reduce_dim_to_string(Dim reduce_dim) {
    switch (reduce_dim) {
        case Dim::C: return "c_dim";
        case Dim::R: return "r_dim";
        case Dim::RC: return "rc_dim";
        case Dim::Z: return "z_dim";
        default: log_assert(false, "Unrecognized ReduceDim supported");
    }
    return "";
}
void tt_reduce::utils::golden_model(const tt_reduce_config& config, vector<tt_tensor*>& inputs, tt_tensor* out) {
    log_assert(inputs.size() == 1, "Incorrect input count for reduce");
    log_assert(inputs[0]->get_data_format() != DataFormat::Invalid, "Input data_format to tt_reduce is invalid");
    log_debug(
        tt::LogOp,
        "Golden -- Reduce Op: {} Dim: {}",
        tt_reduce::utils::reduce_func_to_string(config.reduce_func),
        config.reduce_dim);
    *out = inputs[0]->reduce(config.reduce_func, config.reduce_dim, false, config.z_dim);
    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_reduce is invalid");
}
