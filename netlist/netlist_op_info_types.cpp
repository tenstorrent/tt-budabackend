// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_op_info_types.hpp"

#include "netlist_utils.hpp"
#include "common/size_lib.hpp"
#include "utils/logger.hpp"
using namespace std;

ostream& operator<<(ostream& os, const tt_splice_info& t) {
    os << "tt_splice_info{";
    os << " .index = " << t.index << ",";
    os << " .length = " << t.length << ",";
    os << " .stride = " << t.stride << ",";
    os << "}";
    return os;
}
void verify(const tt_splice_info& t) {
    if (t.index < 0)
        log_fatal("Invalid tt_splice_info::index parsed from netlist");
    if (t.length < 0)
        log_fatal("Invalid tt_splice_info::length parsed from netlist");
    if (t.stride < 0)
        log_fatal("Invalid tt_splice_info::stride parsed from netlist");
}
ostream& operator<<(ostream& os, const tt_op_attributes& t) {
    os << "tt_op_attributes{";
    // General attribute
    os << " .op_type = " << t.op_type << ",";
    os << " .kernel_broadcast = "; for (auto k : t.kernel_broadcast) os << k.first << ", per_t = " << k.second << ",";
    // Normal Matmul Attributes
    if (netlist_utils::is_valid_matmul_op(t.op_type) or netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .m_k = " << t.m_k << ",";
        os << " .u_kt = " << t.u_kt << ",";
        os << " .accumulate = " << t.accumulate << ",";
        os << " .bias = " << t.bias << ",";
        os << " .min_input_buffer = " << t.min_input_buffer << ",";
    }
    // SPU attributes
    if (netlist_utils::is_valid_sfpu_op(t.op_type) or netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .vector = " << t.vector_mode << ",";
        os << " .approximate_mode = " << t.approximate_mode << ",";
    }
    // Relu attributes
    os << " .relu_en = " << t.relu_en << ",";
    os << " .relu_threshold = " << t.relu_threshold << ",";
    os << " .relu_mode = " << netlist_utils::get_relu_mode_string(t.relu_mode) << ",";
    // Dropout related attributes
    if ((netlist_utils::is_valid_sfpu_op(t.op_type) and (netlist_utils::get_valid_sfpu_op(t.op_type) == SfpuOp::Dropout)) or
        netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .probability = " << t.probability << ",";
        os << " .scale = " << t.scale << ",";
        os << " .seed = " << t.seed << ",";
    }
    // Power related attributes
    if ((netlist_utils::is_valid_sfpu_op(t.op_type) and (netlist_utils::get_valid_sfpu_op(t.op_type) == SfpuOp::Power)) or
        netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .exponent = " << t.exponent << ",";
    }
    // LRelu related attributes
    if ((netlist_utils::is_valid_sfpu_op(t.op_type) and (netlist_utils::get_valid_sfpu_op(t.op_type) == SfpuOp::LRelu)) or
        netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .slope = " << t.slope << ",";
    }
    // Reduce related attributes
    if (netlist_utils::is_valid_reduce_op(t.op_type) or netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .reduce_dim = " << t.reduce_dim << ",";
        os << " .z = " << t.z << ",";
        os << " .reduce_type = " << netlist_utils::get_reduce_func_string(t.reduce_type) << ",";
    }
    // Nary Related Attributes
    if (netlist_utils::is_valid_nary_op(t.op_type) or netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .splice_infos = {";
        for (const auto& splice_info : t.splice_infos) {
            os << "" << splice_info << ",";
        }
        os << "},";
        os << " .splice_mode = " << netlist_utils::get_splice_mode_string(t.splice_mode) << ",";
    }
    // Embedding Related Attributes
    if (netlist_utils::is_valid_embedding_op(t.op_type) or netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .num_indices = " << t.num_indices << ",";
    }
    // Matmul Identity related attributes
    if ((netlist_utils::is_valid_matmul_op(t.op_type) or netlist_utils::is_valid_fused_op(t.op_type)) and t.identity) {
        os << " .identity = " << t.identity << ",";
        os << " .num_index_tiles = " << t.num_index_tiles << ",";
        os << " .num_sparse_tiles = " << t.num_sparse_tiles << ",";
        os << " .num_columns = " << t.num_columns << ",";
        os << " .sparse_tile_ptr_bits = " << t.sparse_tile_ptr_bits << ",";
        os << " .sparse_ublock_idx_bits = " << t.sparse_ublock_idx_bits << ",";
        os << " .indices_len = " << t.indices_len << ",";
        os << " .fracture_factor = " << t.fracture_factor << ",";
        os << " .starting_row = " << t.starting_row << ",";
        os << " .act_t = " << t.act_t << ",";
        os << " .num_nz_strips = " << t.num_nz_strips << ",";
    }
    // Fused Op related attributes
    if (netlist_utils::is_valid_fused_op(t.op_type)) {
        os << " .fused_op_id = " << t.fused_op_id << ",";
        os << " .fused_op_output = " << t.fused_op_output << ",";
        os << " .fused_op_inputs_to_pop = {";
        for (const auto& input_to_pop : t.fused_op_inputs_to_pop) {
            os << "" << input_to_pop << ",";
        }
        os << "}";
        os << " .fused_op_inputs_to_pop_last = {";
        for (const auto& input_to_pop_last : t.fused_op_inputs_to_pop_last) {
            os << "" << input_to_pop_last << ",";
        }
        os << "}";
    }
    // End
    os << "}";
    return os;
}
void verify(const tt_op_attributes& t) {
    if (netlist_utils::is_valid_matmul_op(t.op_type)) {
        log_assert(
            (t.m_k > 0) and (t.u_kt > 0),
            "Invalid tt_op_attributes::m_k and tt_op_attributes::u_kt parsed from netlist for matmul op");
        log_assert(
            not(t.accumulate and t.identity),
            "Both tt_op_attributes::accumulate and tt_op_attributes::identity cannot be set at same time for matmul "
            "op");
    } else if (netlist_utils::is_valid_reduce_op(t.op_type)) {
        log_assert(
            (t.reduce_dim != Dim::Invalid) and (t.reduce_type != ReduceFunc::Invalid),
            "Invalid tt_op_attributes::reduce_dim and tt_op_attributes::reduce_type parsed from netlist for reduce op");
    } else if (netlist_utils::get_nary_op(t.op_type) == NaryOp::Splice) {
        for (const auto& splice_info : t.splice_infos) {
            verify(splice_info);
        }
        log_assert(t.splice_mode != SpliceMode::Invalid, "Invalid Splice mode set in attributes for splice op");
    }
    log_assert(
        (t.vector_mode == Dim::R) or (t.vector_mode == Dim::C) or (t.vector_mode == Dim::RC),
        "vector_mode={} can only support R/C/RC(default)",
        t.vector_mode);
}
ostream& operator<<(ostream& os, const tt_op_info& t) {
    os << "tt_op_info{";
    os << " .name = " << t.name << ",";
    os << " .type = " << t.type << ",";
    os << " .grid_loc = " << t.grid_loc << ",";
    os << " .grid_size = " << t.grid_size << ",";
    os << " .input_names = " << t.input_names << ",";
    os << " .forked_dram_input_names = " << t.forked_dram_input_names << ",";
    os << " .input_data_formats = " << t.input_data_formats << ",";
    os << " .intermed_data_format = " << t.intermed_data_format << ",";
    os << " .dest_accumulate_data_format = " << t.dest_accumulate_data_format << ",";
    os << " .output_data_format = " << t.output_data_format << ",";
    os << " .math_fidelity = " << t.math_fidelity << ",";
    os << " .output_dim = " << t.output_dim << ",";
    os << " .buf_size_mb = " << t.buf_size_mb << ",";
    os << " .untilize_output = " << t.untilize_output << ",";
    os << " .gradient_op = " << t.gradient_op << ",";
    os << " .transpose = " << t.transpose << ",";
    os << " .attributes = " << t.attributes << ",";
    os << " .my_op = 0x" << hex << t.my_op << dec << ",";
    os << " .input_tm_ops = {";
    for (const auto& input_tm_it : t.input_tm_ops) {
        os << " .input_" << input_tm_it.first << "_tms = [";
        for (const auto& tm : input_tm_it.second) {
            os << " .tm = " << get<0>(tm) << ", .args = " << get<1>(tm);
        }
        os << "]";
    }
    os << " .op_output_tensor_ptr = 0x" << hex << t.op_output_tensor_ptr << dec;
    os << "}}";
    return os;
}

void verify_gradient_op(const tt_op_info& t) {
    if (t.gradient_op and ((not netlist_utils::is_valid_matmul_op(t.type)) && (t.arch_name != tt::ARCH::WORMHOLE_B0) &&
                           (BinaryOp::Multiply != (netlist_utils::get_binary_op(t.type))) &&
                           (UnaryOp::Datacopy != (netlist_utils::get_unary_op(t.type)))))
        log_fatal("Unsupported tt_op_info::gradient_op set for op {}, type {}", t.name, t.type);
}

void verify(const tt_op_info& t) {
    if (t.intermed_data_format == DataFormat::Invalid)
        log_fatal("Invalid tt_op_info::intermed_data_format parsed from netlist");
    if (t.dest_accumulate_data_format == DataFormat::Invalid)
        log_fatal("Invalid tt_op_info::dest_accumulate_data_format parsed from netlist");
    if (t.output_data_format == DataFormat::Invalid)
        log_fatal("Invalid tt_op_info::output_data_format parsed from netlist");
    if (t.math_fidelity == MathFidelity::Invalid)
        log_fatal("Invalid tt_op_info::math_fidelity parsed from netlist");
    if (t.buf_size_mb < 0)
        log_fatal("Invalid tt_op_info::buf_size_mb parsed from netlist");
    verify(t.output_dim);
    if ((t.output_dim.ublock_order != Dim::R) and (netlist_utils::is_valid_matmul_op(t.type)))
        log_fatal("Invalid tt_op_info::ublock_order must be DIM::R for matmul op");
    verify_gradient_op(t);
    if (t.grid_loc.size() != 2 && t.type != "ethernet_datacopy")
        log_fatal("Invalid tt_op_info::grid_loc parsed from netlist");
    log_assert(
        (t.grid_size_x() > 0) and (t.grid_size_y() > 0),
        "op={} has invalid grid_size=[{},{}]",
        t.name,
        t.grid_size_y(),
        t.grid_size_x());
    if (t.type.empty())
        log_fatal("Invalid tt_op_info::type parsed from netlist");
    if (t.input_names.size() != t.input_data_formats.size())
        log_fatal(
            "Invalid tt_op_info::input_formats -- number of formats must match number of inputs specified");
    log_assert(
        t.input_names.size() >= t.forked_dram_input_names.size(),
        "tt_op_info::input_names.size() must be >= tt_op_info::forked_dram_input_names.size()");
    if (t.untilize_output and (t.output_dim.ublock_order != Dim::R))
        log_fatal("Invalid tt_op_info::untilize_output -- Must have ublock order of Dim::R");
    if (t.untilize_output and (t.output_data_format != DataFormat::Float16) and
        (t.output_data_format != DataFormat::Float16_b) and (t.output_data_format != DataFormat::Float32)) {
        if(t.output_data_format == DataFormat::Int8 or (t.output_data_format == DataFormat::Int32))  {
            log_assert(t.arch_name != tt::ARCH::GRAYSKULL,  "Invalid tt_op_info::untilize_output -- Must have dataformat float16/float16_b/float32 on Grayskull");
        }
        else {
            log_fatal("Invalid tt_op_info::untilize_output -- Data format {} is not supported", t.output_data_format);
        }
    }

    verify(t.attributes);
    // if (netlist_utils::is_valid_matmul_op(t.type)) {
    //     if (t.attributes.accumulate) {
    //         for (const auto& input_dim: t.input_dims){
    //             log_assert(
    //                 t.output_dim.t*t.attributes.z == input_dim.t,
    //                 "Output dim for a matmul with accumulate must have output.t*config.z == input.t"
    //             );
    //         }
    //     }
    // }

    if(netlist_utils::is_valid_reduce_op(t.type) and t.dest_accumulate_data_format == DataFormat::Float32) {
        // tenstorrent/budabackend#1464
        log_assert(
            !netlist_utils::is_exp_a_format(t.input_data_formats[0]),
            "Reduce op {} cannot have input of a-type and dest in F32 format (issue #1464)",
            t.name);
    }
}
int get_mblock_size_tiles(const tt_op_info& op_info) {
    return tt::size::get_mblock_size_in_tiles(
        op_info.output_dim.ublock_ct,
        op_info.output_dim.ublock_rt,
        op_info.output_dim.mblock_m,
        op_info.output_dim.mblock_n);
}

int get_ublock_size_tiles(const tt_op_info& op_info) {
    return tt::size::get_ublock_size_in_tiles(op_info.output_dim.ublock_ct, op_info.output_dim.ublock_rt);
}
