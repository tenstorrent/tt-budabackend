// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "mm_bare.hpp"

#include "common/model/tt_core.hpp"
#include "common/param_lib.hpp"
#include "mm_bare_structs.hpp"
#include "netlist/netlist_utils.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/scoped_timer.hpp"

// Need namespaces because of hlk_args redefinition
namespace matmul_u {
#include "hlks/inc/matmul_u.h"
}

namespace matmul_u_int32 {
#include "hlks/inc/matmul_u_int32.h"
}

namespace matmul_ident {
#include "hlks/inc/matmul_ident.h"
}

string tt_mm_bare_op::get_hlks_file_name_no_extension(bool identity, bool is_int32_matmul) {
    if (identity) {
        return "matmul/matmul_ident";
    } else {
        if (is_int32_matmul) {
            return "matmul/matmul_u_int32";
        } else {
            return "matmul/matmul_u";
        }
    }
}

tt_mm_bare_op::~tt_mm_bare_op() {
}

void tt_mm_bare_op::set_hlk_args_t(
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    std::uint32_t transpose,
    bool identity,
    std::uint32_t num_index_tiles,
    std::uint32_t num_sparse_tiles,
    std::uint32_t num_columns,
    std::uint32_t sparse_tile_ptr_bits,
    std::uint32_t sparse_ublock_idx_bits,
    std::uint32_t block_tile_dim_bits,
    std::uint32_t indices_len,
    // std::uint32_t fracture_factor,
    std::uint32_t starting_row,
    std::uint32_t bias, 
    bool accumulate, 
    std::uint32_t z,
    bool relu_en, 
    std::uint32_t relu_threshold,
    ReluMode relu_mode,
    std::vector<bool> min_buffer_input,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    SfpuOp sfpu_op,
    Dim sfpu_vector_mode,
    bool l1_acc_en,
    bool shared_buffer,
    std::uint32_t act_t,
    bool adv_features_en,
    bool is_int32_matmul,
    bool requant,
    bool dequant,
    float zero_point) {
    if (identity) {
        tt::tt_hlk_args_desc hlk_args_descriptor;
        hlk_args_descriptor.push_scalar_value("block_tile_dim", block_tile_dim);
        hlk_args_descriptor.push_scalar_value("dst_tile_rows", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("dst_tile_cols", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("block_cnt", block_cnt);
        hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
        hlk_args_descriptor.push_scalar_value("in0_block_tile_cnt", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("in1_block_tile_cnt", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("out_block_tile_cnt", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_sub_block", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("gradient_op", gradient_op);
        hlk_args_descriptor.push_scalar_value("transpose", transpose);
        hlk_args_descriptor.push_scalar_value("num_index_tiles", num_index_tiles);
        hlk_args_descriptor.push_scalar_value("num_sparse_tiles", num_sparse_tiles);
        hlk_args_descriptor.push_scalar_value("num_columns", num_columns);
        hlk_args_descriptor.push_scalar_value("sparse_tile_ptr_bits", sparse_tile_ptr_bits);
        hlk_args_descriptor.push_scalar_value("sparse_ublock_idx_bits", sparse_ublock_idx_bits);
        hlk_args_descriptor.push_scalar_value("block_tile_dim_bits", block_tile_dim_bits);
        hlk_args_descriptor.push_scalar_value("indices_len", indices_len);
        hlk_args_descriptor.push_scalar_value("starting_row", starting_row);
        hlk_args_descriptor.push_scalar_value("bias", bias);

        int32_t relu_config = (int32_t)(relu_en ? (static_cast<uint32_t>(relu_en) | static_cast<uint32_t>(relu_mode) | (relu_threshold<<16)) : 0);

        hlk_args_descriptor.push_scalar_value("relu_config", relu_config);

        hlk_args_descriptor.push_scalar_value("l1_acc_en", l1_acc_en);
        hlk_args_descriptor.push_scalar_value("shared_buffer", shared_buffer);
        hlk_args_descriptor.push_scalar_value("act_t", act_t);
        hlk_args_descriptor.push_scalar_value("adv_features_en", adv_features_en);

        cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
    } else {
        tt::tt_hlk_args_desc hlk_args_descriptor;
        hlk_args_descriptor.push_scalar_value("block_tile_dim", block_tile_dim);
        hlk_args_descriptor.push_scalar_value("dst_tile_rows", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("dst_tile_cols", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("block_cnt", block_cnt);
        hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
        hlk_args_descriptor.push_scalar_value("in0_block_tile_cnt", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("in1_block_tile_cnt", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("out_block_tile_cnt", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_sub_block", 0); // unused, set to zero
        hlk_args_descriptor.push_scalar_value("gradient_op", gradient_op);
        hlk_args_descriptor.push_scalar_value("transpose", transpose);
        hlk_args_descriptor.push_scalar_value("bias", bias);
        hlk_args_descriptor.push_scalar_value("accumulate", accumulate);
        hlk_args_descriptor.push_scalar_value("z", z);

        int32_t relu_config = (int32_t)(relu_en ? (static_cast<uint32_t>(relu_en) | static_cast<uint32_t>(relu_mode) | (relu_threshold<<16)) : 0);

        hlk_args_descriptor.push_scalar_value("relu_config", relu_config);

        vector<int32_t> min_input_buffer = {(int32_t) min_buffer_input[0], (int32_t) min_buffer_input[1]};
        hlk_args_descriptor.push_1d_vector_values("min_input_buffer", {(int32_t) min_buffer_input[0], (int32_t) min_buffer_input[1]});

        int max_num_inputs = is_int32_matmul ? matmul_u_int32::MATMUL_U_INT32_MAX_NUM_INPUTS
                                             : matmul_u::MATMUL_U_MAX_NUM_INPUTS;

        hlk_args_descriptor.push_scalar_value("sfpu_op", (int32_t)sfpu_op);
        hlk_args_descriptor.push_scalar_value("sfpu_vector_mode", (int32_t)sfpu_vector_mode);
        hlk_args_descriptor.push_scalar_value("l1_acc_en", l1_acc_en);
        hlk_args_descriptor.push_scalar_value("shared_buffer", shared_buffer);
        hlk_args_descriptor.push_scalar_value("adv_features_en", adv_features_en);

        if (is_int32_matmul) {
            hlk_args_descriptor.push_scalar_value("requant", requant);
            hlk_args_descriptor.push_scalar_value("dequant", dequant);
            union {
                float f;
                int32_t i;
            } f2i = {.f = zero_point};
            hlk_args_descriptor.push_scalar_value("zero_point", f2i.i);
        }

        cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;

    }
    log_debug(
        tt::LogOp,
        "tt_mm_bare_op gradient_op={} batch_cnt={} block_cnt={} block_tile_dim {} num_m {} num_n {} "
        "num_tiles_per_m_sub_block {} num_tiles_per_n_sub_block {} transpose {} identity {}",
        gradient_op,
        batch_cnt,
        block_cnt,
        block_tile_dim,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        transpose,
        identity);
}

tt_mm_bare_op::tt_mm_bare_op(
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
    std::uint32_t gradient_op,
    std::uint32_t transpose,
    bool untilize_output,
    MathFidelity math_fidelity,
    bool fp32_dest_acc_en,
    bool relu_en,
    std::uint32_t relu_threshold,
    ReluMode relu_mode,
    std::vector<DataFormat>& in_data_format,
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    bool identity,
    std::uint32_t num_index_tiles,
    std::uint32_t num_sparse_tiles,
    std::uint32_t num_columns,
    std::uint32_t sparse_tile_ptr_bits,
    std::uint32_t sparse_ublock_idx_bits,
    std::uint32_t block_tile_dim_bits,
    std::uint32_t indices_len,
    std::uint32_t fracture_factor,
    std::uint32_t starting_row, 
    bool bias,
    bool accumulate, 
    std::uint32_t z,
    std::vector<bool> min_buffer_input,
    bool l1_acc_en, 
    std::vector<std::pair<int, bool>> kernel_broadcast,
    SfpuOp sfpu_op,
    Dim sfpu_vector_mode,
    SfpuExecutionThread sfpu_execution_thread,
    std::uint32_t act_t,
    tt::ARCH arch_name,
    StochRndMode stoch_rnd_mode,
    bool requant,
    bool dequant,
    float zero_point,
    const std::vector<std::vector<int>>& input_tile_dims,
    const std::vector<int>& output_tile_dims,
    TileDimReconfigMode is_tile_dim_reconfig_mode) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {
        .block_tile_dim = block_tile_dim,
        .block_cnt = block_cnt,
        .batch_cnt = batch_cnt,
        .num_m_sub_blocks = num_m_sub_blocks,
        .num_n_sub_blocks = num_n_sub_blocks,
        .num_tiles_per_m_sub_block = num_tiles_per_m_sub_block,
        .num_tiles_per_n_sub_block = num_tiles_per_n_sub_block,
        .transpose = transpose,
        .gradient_op = gradient_op,
        .out_data_format = out_data_format,
        .identity = identity,
        .num_index_tiles = num_index_tiles,
        .num_sparse_tiles = num_sparse_tiles,
        .num_columns = num_columns,
        .sparse_tile_ptr_bits = sparse_tile_ptr_bits,
        .sparse_ublock_idx_bits = sparse_ublock_idx_bits,
        .block_tile_dim_bits = block_tile_dim_bits,
        .indices_len = indices_len,
        .fracture_factor = fracture_factor,
        .starting_row = starting_row,
        .grid_shape = grid_shape,
        .bias = bias,
        .accumulate = accumulate,
        .z = static_cast<int>(z),
        .kernel_broadcast = kernel_broadcast,
        .sfpu_op = sfpu_op,
        .sfpu_vector_mode = sfpu_vector_mode,
        .requant = requant,
        .dequant = dequant,
        .zero_point = zero_point,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims
    };

    log_assert(min_buffer_input.size() == 2, "min_buffer_input={} for op={} must be sized 2", min_buffer_input, name);
    std::string root_dir = buda_home();

    bool int_fpu_en = netlist_utils::is_int_format(intermed_data_format);
    for (int i=0;i<in_data_format.size();i++) {
        int_fpu_en|=netlist_utils::is_int_format(in_data_format[i]);
    }

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
        identity,
        num_index_tiles,
        num_sparse_tiles,
        num_columns,
        sparse_tile_ptr_bits,
        sparse_ublock_idx_bits,
        block_tile_dim_bits,
        indices_len,
        starting_row,
        bias,
        accumulate,
        z,
        relu_en,
        relu_threshold,
        relu_mode,
        min_buffer_input,
        kernel_broadcast,
        sfpu_op,
        sfpu_vector_mode,
        l1_acc_en,
        (intermed_data_format == out_data_format),
        act_t,
        arch_name != tt::ARCH::GRAYSKULL,
        int_fpu_en,
        requant,
        dequant,
        zero_point
        );

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name_no_extension(identity, int_fpu_en) + ".cpp");

    for (int i=0; i<in_data_format.size(); i++) {
        set_hlk_operand_dataformat_all_cores((HlkOperand)(HlkOperand::in0+(HlkOperand)i), in_data_format[i]);
    }

    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, intermed_data_format);
    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = 0;
    this->relu_threshold = 0;
    this->relu_mode = ReluMode::None;
    this->arch_name = arch_name;
    this->pack_l1_acc_en = pack_l1_acc_en;
    this->sfpu_execution_thread = sfpu_execution_thread;
    this->stoch_rnd_mode = stoch_rnd_mode;
    this->int_fpu_en =  int_fpu_en;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    for (int i = 0; i < input_tile_dims.size(); i++) {
        array<uint32_t, 2> input_i_tile_dims = {(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]};
        this->input_tile_dims.push_back(input_i_tile_dims);
    }
    this->is_tile_dim_reconfig_mode = is_tile_dim_reconfig_mode;
    this->kernel_broadcasts = kernel_broadcast;
    int max_num_inputs;
    if (identity) {
        max_num_inputs = matmul_ident::MATMUL_IDENT_MAX_NUM_INPUTS;
    } else {
        max_num_inputs =
            int_fpu_en ? matmul_u_int32::MATMUL_U_INT32_MAX_NUM_INPUTS : matmul_u::MATMUL_U_MAX_NUM_INPUTS;
    }
    for (int i = this->kernel_broadcasts.size(); i < max_num_inputs; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }
}

std::string tt_mm_bare_op::type_to_str() {
    std::string ret = "tt_mm_bare_op";
    return ret;
}

void tt_mm_bare_op::model(vector<tt_tensor*>& inputs, tt_tensor* out) {
     tt_matmul::utils::golden_model(m_config, inputs, out);
 }

ostream& operator<<(ostream& os, const CompressedIndexControlEntry& t) {
    os << "CompressedIndexControlEntry{";
    os << " .push_tiles=" << t.push_tiles << ",";
    os << " .continue_loop=" << t.continue_loop << ",";
    os << " .loop=" << t.loop << ",";
    os << " .control=" << t.control << ",";
    os << "}";
    return os;
}
ostream& operator<<(ostream& os, const CompressedIndexDataEntry& t) {
    os << "CompressedIndexDataEntry{";
    os << " .data_lower='h" << std::hex << t.data_lower << std::dec << ",";
    os << " .data_upper='h" << std::hex << t.data_upper << std::dec << ",";
    os << " .last_out_block=" << t.last_out_block << ",";
    os << " .last_tile=" << t.last_tile << ",";
    os << "}";
    return os;
}
ostream& operator<<(ostream& os, const CompressedIndex& t) {
    os << "CompressedIndex{";
    os << " .val='h" << std::hex << t.val << std::dec << ",";
    if (t.ctrl_fields.control) {
        os << " .ctrl_fields=" << t.ctrl_fields << ",";
    } else {
        os << " .data_fields=" << t.data_fields << ",";
    }
    os << "}";
    return os;
}
void tt_matmul::utils::golden_model(const tt_matmul_config& config, vector<tt_tensor*>& inputs, tt_tensor* out) {
    log_assert(inputs.size() == (((config.identity && config.bias) || (config.bias && (config.requant||config.dequant))) ? 4 : (config.identity || config.bias || config.requant || config.dequant) ? 3 : 2), "Incorrect number of inputs to matmul");
    log_debug(tt::LogOp, "Golden -- hlk -- Matmul");
    log_debug(tt::LogOp, "full matmul in0 dim {}", inputs[0]->get_shape());
    log_debug(tt::LogOp, "full matmul in1 dim {}", inputs[1]->get_shape());
    if (config.transpose) {
        *inputs[1] = inputs[1]->transpose_xy(false, false, true);  // transpose tile
    }

    log_assert(inputs[0]->get_data_format() != DataFormat::Invalid,"Input 0 data_format to tt_matmul is invalid");
    log_assert(inputs[1]->get_data_format() != DataFormat::Invalid,"Input 1 data_format to tt_matmul is invalid");

    // Reduce input tensor tiles to their tile dims.
    inputs[0]->clear_tile_values(config.input_tile_dims[0][0], config.input_tile_dims[0][1]);
    inputs[1]->clear_tile_values(config.input_tile_dims[1][0], config.input_tile_dims[1][1]);

    bool is_int_matmul = false;
    for (int i = 0; i < inputs.size(); i++) {
        DataFormat input_df = inputs[i]->get_data_format();
        if (input_df == DataFormat::Int8 || input_df == DataFormat::Int32) {
            is_int_matmul = true;
        }
    }

    if (config.identity) {
        log_assert(inputs[2]->get_data_format() != DataFormat::Invalid,"Input 2 data_format to tt_matmul is invalid");
        // FIXME: may need to determine how t splits up here
        // Need to split inputs and combine things based off the inputs/outputs
        auto sparse_weights_per_row = tensor_lib::split_merge_ops::vsplit(*inputs[0], config.grid_shape.r, false);
        auto activations_per_core = tensor_lib::split_merge_ops::hsplit(*inputs[1], config.grid_shape.c, false);
        auto encodings_per_row = tensor_lib::split_merge_ops::vsplit(*inputs[2], config.grid_shape.r, false);

        vector<vector<tt_tensor>> multi_core_outputs = {};
        // compute the grid of outputs
        for (int core_r = 0; core_r < config.grid_shape.r; core_r++) {
            auto sparse_weights_per_col = tensor_lib::split_merge_ops::hsplit(sparse_weights_per_row[core_r], config.fracture_factor, false);
            auto encodings_per_col = tensor_lib::split_merge_ops::hsplit(encodings_per_row[core_r], config.fracture_factor, false);

            int c_to_frac_factor = config.grid_shape.c / config.fracture_factor;

            multi_core_outputs.push_back(vector<tt_tensor>(config.grid_shape.c));
            for (int core_c = 0; core_c < config.grid_shape.c; core_c++) {
                calculate_identity_ljb(
                    config,
                    &multi_core_outputs.at(core_r).at(core_c),              // output
                    &activations_per_core.at(core_c),                       // activations split up
                    &sparse_weights_per_col.at(core_c / c_to_frac_factor),  // sparse_weights
                    &encodings_per_col.at(core_c / c_to_frac_factor)        // encodings
                );
            }
        }

        // recombine the outputs
        vector<tt_tensor> row_outputs(config.grid_shape.r);
        for (int core_r = 0; core_r < config.grid_shape.r; core_r++) {
            row_outputs.at(core_r) = tensor_lib::split_merge_ops::hmerge(multi_core_outputs.at(core_r), false);
        }
        *out = tensor_lib::split_merge_ops::vmerge(row_outputs, false);
        if (config.bias) {
            log_assert(
                inputs[3]->get_data_format() != DataFormat::Invalid, "Input 3 data_format to tt_matmul is invalid");
            *out = out->add(*inputs[3]);
        }
    } else if (config.bias) {
        log_assert(inputs[2]->get_data_format() != DataFormat::Invalid, "Input 2 data_format to tt_matmul is invalid");
        log_assert(
            (config.block_cnt * config.block_tile_dim) == inputs[1]->getrt(),
            "Matmul Attributes not valid -- block_cnt(m_k={}) and block_tile_dim(u_kt={}) need to be the k-dim={}",
            config.block_cnt,
            config.block_tile_dim,
            inputs[1]->getrt());
        tt_tensor matmul_output = inputs[0]->matmul(*inputs[1], false, true);
        if (config.accumulate) {
            log_assert (
                matmul_output.getz() > 1,
                "When accumulate is set for matmul, input t > 1"
            );
            matmul_output = matmul_output.reduce(ReduceFunc::Sum, Dim::Z, false, config.z);
        } 
        log_assert(
            matmul_output.same_shape(*inputs[2]),
            "Matmul bias shape={} must be same shape as matmul output shape={}", 
            inputs[2]->get_shape(),
            matmul_output.get_shape()
        );

        if (is_int_matmul) {
            *out = matmul_output.add(*inputs[2]);
        } else {
            *out = matmul_output.add(inputs[2]->broadcast_within_tiles(Dim::R, false));
        }

        if (is_int_matmul && (config.dequant || config.requant)) {
            bool max_value_exceeded = out->check_for_max_abs_value(std::pow(2, 24));
            if (max_value_exceeded) {
                log_warning(
                    tt::LogOp,
                    "Int matmul with requant/dequant produced values bigger 2^24 before feeding these values into "
                    "fused requant/dequant, this can lead to imprecisions in golden implementation, "
                    "due to golden implementation using float32 for integer ops");
            }
        }
        if (config.requant) {
            tensor_lib::basic_ops::eltwise_binary(*out, *out, *inputs[3], tile_lib::binary::multiply);
            tt_tensor tmp(out->get_shape(), out->get_data_format());
            tmp.init_to_input_value(config.zero_point);
            tensor_lib::basic_ops::eltwise_binary(*out, *out, tmp, tile_lib::binary::add);
        } else if (config.dequant) {
            tt_tensor tmp(out->get_shape(), out->get_data_format());
            tmp.init_to_input_value(config.zero_point);
            tensor_lib::basic_ops::eltwise_binary(*out, *out, tmp, tile_lib::binary::add);
            tensor_lib::basic_ops::eltwise_binary(*out, *out, *inputs[3], tile_lib::binary::multiply);
        }
    } else {
        log_assert(
            (config.block_cnt * config.block_tile_dim) == inputs[1]->getrt(),
            "Matmul Attributes not valid -- block_cnt(m_k={}) and block_tile_dim(u_kt={}) need to be the k-dim={}",
            config.block_cnt,
            config.block_tile_dim,
            inputs[1]->getrt());
        tt_tensor matmul_output = inputs[0]->matmul(*inputs[1], false, true);
        if (config.accumulate) {
            log_assert (
                matmul_output.getz() > 1,
                "When accumulate is set for matmul, input t > 1"
            );
            matmul_output = matmul_output.reduce(ReduceFunc::Sum, Dim::Z, false, config.z);
        }
        *out = matmul_output;
        if (is_int_matmul && (config.dequant || config.requant)) {
            bool max_value_exceeded = out->check_for_max_abs_value(std::pow(2, 24));
            log_assert(!max_value_exceeded, "Matmul output value exceeds 24-bit max value")
        }
        if (config.requant) {
            tensor_lib::basic_ops::eltwise_binary(*out, *out, *inputs[2], tile_lib::binary::multiply);
            tt_tensor tmp(out->get_shape(), out->get_data_format());
            tmp.init_to_input_value(config.zero_point);
            tensor_lib::basic_ops::eltwise_binary(*out, *out, tmp, tile_lib::binary::add);
        } else if (config.dequant) {
            tt_tensor tmp(out->get_shape(), out->get_data_format());
            tmp.init_to_input_value(config.zero_point);
            tensor_lib::basic_ops::eltwise_binary(*out, *out, tmp, tile_lib::binary::add);
            tensor_lib::basic_ops::eltwise_binary(*out, *out, *inputs[2], tile_lib::binary::multiply);
        }
    }

    switch (config.sfpu_op) {
        case SfpuOp::Gelu:
            tensor_lib::basic_ops::eltwise_unary(*out, *out, config.sfpu_vector_mode, tile_lib::unary::gelu);
            break;
        case SfpuOp::Invalid:
            break;
        default:     
            log_assert (
                false,  
                "Unsupported sfpu op fused with matmul! Only Gelu is supported!"
            );
            break;
    }

    // Todo: pp, properly fix this once bias tiny tile support testing is done
    // Tensor dims get set to 1x32 in some cases here, and tile_dims get set as well to 1x32, and when the following
    // golden logic takes on, it recreates the tensors looking only those 1x32 dims, filling others with zero, leading
    // to failures.
    if (config.bias &&
        (out->tile_tensor[0][0][0][0].tile_height != 32 || out->tile_tensor[0][0][0][0].tile_width != 32)) {
        for (size_t iw = 0; iw < out->tile_tensor.size(); iw++) {
            for (size_t iz = 0; iz < out->tile_tensor[iw].size(); iz++) {
                for (size_t ir = 0; ir < out->tile_tensor[iw][iz].size(); ir++) {
                    for (size_t ic = 0; ic < out->tile_tensor[iw][iz][ir].size(); ic++) {
                        out->tile_tensor[iw][iz][ir][ic].tile_height = 32;
                        out->tile_tensor[iw][iz][ir][ic].tile_width = 32;
                    }
                }
            }
        }
    }

    // Reduce output tensor tiles to specified tile dims.
    int output_tile_dim_r = config.output_tile_dims[0];
    int output_tile_dim_c = config.output_tile_dims[1];

    out->clear_tile_values(output_tile_dim_r, output_tile_dim_c);

    // Set output dims of tensor to the ones specified in netlist
    // Example:
    //   if input0 tile dims are 16x32, and input1 tile dims are 32x32, logical output tile dims will be 16x32, and other values will be zeroed (see above)
    //   But if specified output_tile_dims are, for example, 32x32, we need to set tensor tile height/width to
    //   those values since the rest of the stack expects so.
    out->metadata.shape.tile_height = config.output_tile_dims[0];
    out->metadata.shape.tile_width = config.output_tile_dims[1];

    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_matmul is invalid");
    log_debug(tt::LogOp, "matmul out dim {}", out->get_shape());
}

void tt_matmul::utils::calculate_identity(
    const tt_matmul_config& config,
    tt_tensor* output,
    tt_tensor* activations,
    tt_tensor* sparse_input,
    tt_tensor* indexing_controls) {
    // Derive flatted uint32_t encodings and decode them
    vector<float> flattened_controls;
    indexing_controls->untilize_to_flat_tensor_data(true, false, false, flattened_controls);
    vector<CompressedIndex> decoded_controls = {};
    for (const auto& encoded_control : flattened_controls) {
        uint32_t* uint_encoded_control_ptr = (uint32_t*)&encoded_control;
        uint32_t uint_encoded_control = *uint_encoded_control_ptr;
        decoded_controls.push_back(uint_encoded_control);
    }

    // Helper Variables from args
    uint32_t inner_r = config.num_tiles_per_m_sub_block;  // inner row block size in tiles
    uint32_t inner_c = config.num_tiles_per_n_sub_block;  // inner column block size in tiles
    uint32_t outer_r = config.num_m_sub_blocks;           // outer row block size (in inner row blocks)
    uint32_t outer_c = config.num_n_sub_blocks;           // outer column block size (in inner column blocks)
    int out_row_tile_cnt = inner_r * outer_r;
    int act_block_tile_cnt = inner_r * inner_c;
    int curr_out_rt = config.starting_row;
    int curr_loop_count = -1;

    log_assert(
        (sparse_input->getct() == 1) or (sparse_input->getrt() == 1),
        "Identity Matmul:: sparse_input dims must be a row or column vector of tiles -- dims = {}",
        sparse_input->get_shape());
    log_assert(
        (indexing_controls->getct() == 1) or (indexing_controls->getrt() == 1),
        "Identity Matmul:: indexing_controls (encodings) dims must be a row or column vector of tiles -- dims = {}",
        indexing_controls->get_shape());

    // Calculate output shape.
    *output =
        tt_tensor(tt_shape{.rt = inner_r * outer_r, .ct = inner_c * outer_c, .z = 1, .w = 1}, config.out_data_format);
    output->set_number(0.0f);
    log_debug(tt::LogOp, "matmul identity output dim {}", output->get_shape());

    // Go through and process matmul
    int index = 0;
    log_assert(
        config.indices_len <= decoded_controls.size(),
        "Identity Matmul:: indices_len={} must be smaller or equal to encoding size = {}",
        index,
        decoded_controls.size());
    int act_block_offset = 0;
    while (index < config.indices_len) {  // Loop will modify index internally
        log_assert(
            index < decoded_controls.size(),
            "Identity Matmul:: Accessing index={}/{} for indexing input is outside of bounds",
            index,
            decoded_controls.size() - 1);
        CompressedIndex decoded_control = decoded_controls.at(index++);
        log_trace(tt::LogGolden, "Decoded Instruction #{} from Encodings -- {}", index - 1, decoded_control);
        if (decoded_control.ctrl_fields.control) {
            if (decoded_control.ctrl_fields.loop) {
                // Loop Start Condition -- saving loop count from data lower
                log_assert(
                    not decoded_control.ctrl_fields.continue_loop,
                    "Cannot have both 'loop' and 'continue_loop' bits set in indexing control -- Encoding={}",
                    decoded_control);
                log_assert(
                    not decoded_control.ctrl_fields.push_tiles,
                    "Cannot have both 'loop' and 'push_tiles' bits set in indexing control -- Encoding={}",
                    decoded_control);

                curr_loop_count = decoded_control.data_fields.data_lower;
            } else if (decoded_control.ctrl_fields.continue_loop) {
                // Loop End/Continue Condition -- decrement loop count and set PC back to data_lower
                log_assert(
                    not decoded_control.ctrl_fields.push_tiles,
                    "Cannot have both 'continue_loop' and 'push_tiles' bits set in indexing control -- Encoding={}",
                    decoded_control);
                log_assert(
                    curr_loop_count >= 0,
                    "Hitting a 'continue' command in encoded indexing controls when loop_count remaining is -1 --> "
                    "indicates a missing 'loop' command usually -- Encoding={}",
                    decoded_control);
                curr_loop_count--;
                if (curr_loop_count > 0) {
                    index = decoded_control.data_fields.data_lower;
                }
            } else if (decoded_control.ctrl_fields.push_tiles) {
                // Push Condition -- Golden does not care really --> Error checking really.
                // Need to remove some number of output rows.
                // TODO -- Check this....
                continue;
            }
        }
        for (int n = 0; n < outer_c; n++) {
            bool last_out_block = false;
            while (not last_out_block) {
                for (int r = 0; r < inner_r; r++) {
                    bool last_tile = false;
                    while (not last_tile) {
                        int output_tile_offset = (curr_out_rt * config.num_columns);
                        log_debug(
                            tt::LogGolden,
                            "output_tile_offset={} -- curr_out_rt={} -- num_columns={}",
                            output_tile_offset,
                            curr_out_rt,
                            config.num_columns);
                        log_assert(
                            index < decoded_controls.size(),
                            "Identity Matmul:: Accessing index={}/{} for indexing input is outside of bounds",
                            index,
                            decoded_controls.size() - 1);
                        CompressedIndex decoded_control = decoded_controls.at(index++);
                        log_trace(
                            tt::LogGolden, "Decoded Instruction #{} from Encodings -- {}", index - 1, decoded_control);
                        int sparse_index =
                            decoded_control.data_fields.data_lower;  // Sparse_index --> Absolute index --> TODO: Check
                                                                     // if there is any blocking
                        log_assert(
                            sparse_index < sparse_input->total_tiles(),
                            "Sparse Index={} is larger than the size of the sparse input={}",
                            sparse_index,
                            sparse_input->get_shape());
                        for (int c = 0; c < inner_c; c++) {
                            // Get the row/column for the block, and get the tile R,C then convert to flat index
                            int act_block_c = act_block_offset % outer_c;
                            int act_block_r = act_block_offset / outer_c;
                            int relative_act_c = c;
                            int relative_act_r = r;
                            int act_index = (relative_act_r + act_block_r * inner_r) * (inner_c * outer_c) +
                                            act_block_c + relative_act_c;

                            int relative_dst_index = c;
                            int output_index =
                                output_tile_offset + relative_dst_index;  // output is based off curr_out_rt which is
                                                                          // the row index, so no blocking is needed.

                            log_debug(
                                tt::LogGolden,
                                "act_block=[r={}, c={}] relative_act=[r={},c={}] -- Flattened Act Index={}",
                                act_block_r,
                                act_block_c,
                                relative_act_r,
                                relative_act_c,
                                act_index);
                            log_debug(
                                tt::LogGolden,
                                "Accumulating onto output_index={} -- act_index={} -- sparse_index={}",
                                output_index,
                                act_index,
                                sparse_index);
                            tt_tile* output_tile = output->get_tile_ptr_from_flat_index(output_index, Dim::R);
                            tt_tile* activation_tile = activations->get_tile_ptr_from_flat_index(act_index, Dim::R);
                            tt_tile* weights_tile = sparse_input->get_tile_ptr_from_flat_index(sparse_index, Dim::R);
                            const auto matmul_output_tile = weights_tile->matmul(*activation_tile);  // matmul
                            *output_tile += matmul_output_tile;                                      // Accumulation
                        }
                        curr_out_rt = decoded_control.data_fields.data_upper;
                        last_tile = decoded_control.data_fields.last_tile;
                        last_out_block = decoded_control.data_fields.last_out_block;
                    }
                }
            }
            // Done with this act block
            act_block_offset++;
        }
    }
}

void tt_matmul::utils::calculate_identity_ljb(
    const tt_matmul_config& config,
    tt_tensor* output,
    tt_tensor* activations,
    tt_tensor* sparse_input,
    tt_tensor* indexing_controls) {
    // Derive the structure from encodings
    vector<float> flattened_controls;
    indexing_controls->untilize_to_flat_tensor_data(true, false, false, flattened_controls);
    strip_info_struct* strip_info_ptr = reinterpret_cast<strip_info_struct*>(flattened_controls.data());

    bool print_debug = 0;

    // Helper Variables from args
    uint32_t inner_r = config.num_tiles_per_m_sub_block;  // inner row block size in tiles
    uint32_t inner_c = config.num_tiles_per_n_sub_block;  // inner column block size in tiles
    uint32_t outer_r = config.num_m_sub_blocks;           // outer row block size (in inner row blocks)
    uint32_t outer_c = config.num_n_sub_blocks;           // outer column block size (in inner column blocks)
    uint32_t ublock_tile_index_bits = 16 - config.sparse_tile_ptr_bits;
    uint32_t num_tiles_in_ublock_bits = 16 - config.sparse_ublock_idx_bits;
    uint32_t ublock_index_bits = config.sparse_ublock_idx_bits;
    uint32_t ublock_tile_inner_d_bits = config.block_tile_dim_bits;
    uint32_t ublock_tile_inner_r_bits = ublock_tile_index_bits - config.block_tile_dim_bits;

    log_assert((1<<ublock_tile_inner_r_bits) >= inner_r,"Not enough bits to store inner_r aka ublock_rt dim in ublock_tile_index");
    log_assert((1<<ublock_tile_inner_d_bits) >= config.block_tile_dim, "Not enough bits to store inner_d aka ublock_kt dim in ublock_tile_index");

    log_assert(
        (1 << config.sparse_tile_ptr_bits) >= ((sparse_input->getct()/config.grid_shape.c) / config.fracture_factor),
        "Not enough bits to store sparse tile ptr");
    log_assert((1<<config.sparse_ublock_idx_bits) >= outer_r, "Not enough bits to store ublock index");

    int out_row_tile_cnt = inner_r * outer_r;
    int out_block_tile_cnt = inner_r * inner_c;
    int curr_out_rt = config.starting_row;
    int curr_loop_count = -1;

    auto get_strip_index = [](strip_info_struct const* strip_info) -> uint32_t {
        uint32_t enc = (uint32_t(strip_info->enc1) << 16u) | uint32_t(strip_info->enc0);
        return enc & ((1u << 30u) - 1);
    };

    auto get_last_strip_in_row = [](strip_info_struct const* strip_info) -> bool {
        return (bool(strip_info->enc1 & (1u << 14u)));
    };

    auto get_last_strip_in_tile = [](strip_info_struct const* strip_info) -> bool {
        return (bool(strip_info->enc1 & (1u << 15u)));
    };

    // Calculate output shape.
    *output = tt_tensor(
        tt_shape{.rt = inner_r * outer_r, .ct = inner_c * outer_c, .z = config.batch_cnt, .w = 1, .tile_height = sparse_input->get_tile_height(), .tile_width = activations->get_tile_width()},
        config.out_data_format);
    output->set_number(0.0f);
    log_debug(tt::LogOp, "matmul identity output dim {}", output->get_shape());

    uintptr_t saved_address_ptr = reinterpret_cast<uintptr_t>(flattened_controls.data());
    // Go through and process matmul
    int out_batch = 0;
    while (out_batch < config.batch_cnt) {
        {
            // If appropriate wait for new sparsity info tile to be in the
            // receive buffer for Operand 2
            bool last_out = get_last_strip_in_row(strip_info_ptr);

            // check whether left input strip is zeros
            uint32_t strip_index = get_strip_index(strip_info_ptr);

            if (print_debug)
            {
                std::cout << fmt::format("~~ batch: {}", out_batch) << std::endl;
                std::cout << fmt::format("~~~~ si: {}", strip_index) << std::endl;
                std::cout << fmt::format("~~~~ + last_strip_in_row: {}", last_out) << std::endl;
                std::cout << fmt::format("~~~~ + last_strip_in_tile: {}", get_last_strip_in_tile(strip_info_ptr)) << std::endl;
            }

            {
                // Execute the strip computation
                // Point left input unpacker to the correct place in L1
                // and execute the computation

                // below two loops move through output u-blocks
                int nz_ublocks_in_strip = strip_info_ptr->nz_ublocks;
                int current_index = 0;
                int ublock_start_index = 0;
                int current_ublock_index = 0;
                int nz_tiles_in_ublock = 0;
                int first_tile_index = 0;
                int number_of_bytes_for_index_array = 0;

                log_trace(tt::LogOp, " -- num_ublocks={} in strip={}", nz_ublocks_in_strip, strip_index);
                if (print_debug) std::cout << fmt::format("~~~~~~ nz_ublocks: {}", nz_ublocks_in_strip) << std::endl;

                // Pre-calculate all the sizes for the encoding info ahead of time.
                for (int ublock_idx = 0; ublock_idx < nz_ublocks_in_strip; ublock_idx++) {
                    std::uint16_t encoded_index = strip_info_ptr->index_array[number_of_bytes_for_index_array];
                    int nz_tiles_in_ublock = encoded_index >> ublock_index_bits;
                    if (nz_tiles_in_ublock == 0) {
                        nz_tiles_in_ublock = (1 << num_tiles_in_ublock_bits);
                    }
                    log_trace(tt::LogOp, " -- num_tiles={} in ublock{}", nz_tiles_in_ublock, ublock_idx);
                    number_of_bytes_for_index_array += nz_tiles_in_ublock + 1;  // number of bytes for each tile;
                }
                log_trace(tt::LogOp, " -- number_of_bytes_for_index_array={}", number_of_bytes_for_index_array);

                uint16_t ublock_cntr = 0;
                for (int out_r = 0; out_r < outer_r; out_r++) {
                    // Increment to next sparse u-block - current index expected
                    // to be pointing to the next u-block index byte
                    ublock_start_index = current_index;  // this
                    if (ublock_cntr >= nz_ublocks_in_strip)
                        continue;
                    current_ublock_index = strip_info_ptr->index_array[current_index] & ((1 << ublock_index_bits) - 1);
                    bool left_ublock_zero = (current_ublock_index != out_r);
                    if (!left_ublock_zero)
                        ublock_cntr++;
                    else
                        continue;

                    if (print_debug) std::cout << fmt::format("~~~~~~~~ ublock_index: {}", current_ublock_index) << std::endl;

                    for (int out_c = 0; out_c < outer_c; out_c++) {
                        if (!left_ublock_zero) {
                            // Reset to beginning of sparse u-block
                            current_index = ublock_start_index;
                            std::uint16_t encoded_index = strip_info_ptr->index_array[current_index++];
                            current_ublock_index =
                                strip_info_ptr->index_array[current_index] & ((1 << ublock_index_bits) - 1);
                            nz_tiles_in_ublock = encoded_index >> ublock_index_bits;
                            if (0 == nz_tiles_in_ublock) {
                                nz_tiles_in_ublock = (1 << num_tiles_in_ublock_bits);
                            }
                            if (print_debug && out_c == 0) std::cout << fmt::format("~~~~~~~~ num_matmuls: {}", nz_tiles_in_ublock) << std::endl;
                            first_tile_index = current_index;

                            // Compute MM Sub Block
                            // Below computes for 1 full output ublock
                            bool out_of_tile_range = false;
                            for (int in_r = 0; in_r < inner_r; in_r++) {
                                for (int in_d = 0; in_d < config.block_tile_dim; in_d++) {
                                    // ## move inner d to be central loop so inner loop would be clean and
                                    // srca tile reused
                                    int encoded_index = strip_info_ptr->index_array[current_index];
                                    int encoded_in_r = (encoded_index & ((1 << ublock_tile_index_bits) - 1))>>(ublock_tile_inner_d_bits);
                                    int encoded_in_d = (encoded_index & ((1 << ublock_tile_index_bits) - 1))&((1<<ublock_tile_inner_d_bits)-1);
                                    bool left_tile_zero = ((encoded_in_r * config.block_tile_dim + encoded_in_d) !=
                                                           (in_r * config.block_tile_dim + in_d)) ||
                                                          out_of_tile_range;

                                    int dst_index = 0;
                                    if (!left_tile_zero) {
                                        // out_r*inner_r*inner_d + in_r*inner_d + in_d;
                                        current_index++;
                                        int sparse_index = encoded_index >> ublock_tile_index_bits;
                                        if (print_debug && out_c == 0) std::cout << fmt::format("~~~~~~~~~~ in0_index: {}", sparse_index) << std::endl;
                                        if (print_debug && out_c == 0) std::cout << fmt::format("~~~~~~~~~~ in1_index: {}, {}", encoded_in_r, encoded_in_d) << std::endl;
                                        for (int in_c = 0; in_c < inner_c; in_c++) {
                                            // Calculate activation tile we want to use
                                            int act_block_r = strip_index;
                                            int act_block_c = out_c;
                                            int act_relative_r = in_d;
                                            int act_relative_c = dst_index;
                                            int act_r = act_block_r * config.block_tile_dim + act_relative_r;
                                            int final_act_z = act_r / activations->get_shape().rt;
                                            int final_act_r = act_r % activations->get_shape().rt;
                                            int final_act_c = act_block_c * inner_c + act_relative_c;

                                            int output_block_r = out_r;
                                            int output_block_c = out_c;
                                            int output_relative_c = in_c;
                                            int output_relative_r = in_r;
                                            int final_output_r = output_block_r * inner_r + output_relative_r;
                                            int final_output_c = act_block_c * inner_c + act_relative_c;
                                            uint32_t output_index =
                                                (out_r * inner_r + in_r) * inner_c * outer_c + out_c * inner_c + in_c;
                                            log_trace(
                                                tt::LogOp,
                                                "strip_index={} -- O[r={},c={}] = A[r={},c={}] * S{}",
                                                strip_index,
                                                final_output_r,
                                                final_output_c,
                                                final_act_r,
                                                final_act_c,
                                                sparse_index);

                                            tt_tile* output_tile =
                                                output->get_tile_ptr(final_output_r, final_output_c, out_batch, 0);
                                            tt_tile* activation_tile =
                                                activations->get_tile_ptr(final_act_r, final_act_c, final_act_z, 0);
                                            tt_tile* weights_tile = sparse_input->get_tile_ptr(0, sparse_index, 0, 0);
                                            const auto matmul_output_tile =
                                                weights_tile->matmul(*activation_tile);  // matmul
                                            *output_tile += matmul_output_tile;          // Accumulation
                                            dst_index++;

                                            if (print_debug && out_c == 0)
                                            {
                                                std::cout << fmt::format("~~~~~~~~~~~~ in0[{}]", sparse_index) << std::endl;
                                                std::cout << fmt::format("~~~~~~~~~~~~ act[{}, {}]", final_act_r, final_act_c) << std::endl;
                                                std::cout << fmt::format("~~~~~~~~~~~~ out[{}, {}, {}]", out_batch, final_output_r, final_output_c) << std::endl;

                                                // In0
                                                uint32_t in0_sum = weights_tile->hash_one();
                                                uint32_t in0_prod = weights_tile->hash_two();
                                                std::cout << fmt::format("~~~~~~~~~~~~~~ in0_hash_sum = {}", in0_sum) << std::endl;
                                                std::cout << fmt::format("~~~~~~~~~~~~~~ in0_hash_prod = {}", in0_prod) << std::endl;

                                                // In1
                                                uint32_t in1_sum = activation_tile->hash_one();
                                                uint32_t in1_prod = activation_tile->hash_two();
                                                std::cout << fmt::format("~~~~~~~~~~~~~~ in1_hash_sum = {}", in1_sum) << std::endl;
                                                std::cout << fmt::format("~~~~~~~~~~~~~~ in1_hash_prod = {}", in1_prod) << std::endl;

                                                // Out
                                                uint32_t out_sum = matmul_output_tile.hash_one();
                                                uint32_t out_prod = matmul_output_tile.hash_two();
                                                std::cout << fmt::format("~~~~~~~~~~~~~~ out_hash_sum = {}", out_sum) << std::endl;
                                                std::cout << fmt::format("~~~~~~~~~~~~~~ out_hash_prod = {}", out_prod) << std::endl;
                                            }
                                        }
                                        if ((current_index - first_tile_index) == nz_tiles_in_ublock)
                                            out_of_tile_range = true;
                                    }
                                }
                            }
                        }
                    }
                }

                // Move info pointer to next strip
                uintptr_t next_strip_info_base_ptr = reinterpret_cast<uintptr_t>(strip_info_ptr) +
                                                     sizeof(strip_info_struct) +
                                                     number_of_bytes_for_index_array * sizeof(std::uint16_t);
                if (get_last_strip_in_tile(strip_info_ptr)) {
                    // If this is the last strip in the encoding tile, we increment by a full tile size instead
                    int tile_size = constants::TILE_HEIGHT * constants::TILE_WIDTH * sizeof(uint32_t);
                    next_strip_info_base_ptr = saved_address_ptr + tile_size;
                    saved_address_ptr = next_strip_info_base_ptr;
                }
                strip_info_ptr = reinterpret_cast<strip_info_struct*>(next_strip_info_base_ptr);
            }

            out_batch += int(last_out);
        }
    }
}
