// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_utils.hpp"

// Needed in order to call create_ops() for each different op type
#include <algorithm>
#include <ostream>

#include "ops/depthwise_op.hpp"
#include "ops/eltwise_binary_bare.hpp"
#include "ops/eltwise_unary_sfpu_bare.hpp"
#include "ops/embedding_op.hpp"
#include "ops/tilizer.hpp"
#include "ops/fused_op.hpp"
#include "ops/mm_bare.hpp"
#include "ops/nary_bare.hpp"
#include "ops/reduce_bare.hpp"
#include "ops/unary_bare.hpp"

string netlist_utils::get_string(Dim dim) {
    switch (dim) {
        case Dim::None: return "None";
        case Dim::R: return "R";
        case Dim::C: return "C";
        case Dim::Z: return "Z";
        case Dim::RC: return "RC";
        case Dim::ZR: return "ZR";
        default: return "Invalid";
    }
}

ostream& operator<<(ostream& os, const Dim& t) {
    os << "Dim::" << netlist_utils::get_string(t);
    return os;
}

Dim netlist_utils::get_dim_enum(const string& t) {
    if (t == "r") {
        return Dim::R;
    } else if (t == "c") {
        return Dim::C;
    } else if (t == "rc") {
        return Dim::RC;
    } else if (t == "z") {
        return Dim::Z;
    } else {
        return Dim::Invalid;
    }
}

ReluMode netlist_utils::get_relu_mode_enum(const string& t) {
    if (t == "min") {
        return ReluMode::Min;
    } else if (t == "max") {
        return ReluMode::Max;
    } else if (t == "none") {
        return ReluMode::None;
    } else {
        return ReluMode::Invalid;
    }
}

SfpuExecutionThread netlist_utils::get_sfpu_execution_thread(const string& sfpu_execution_thread)
{
    if (sfpu_execution_thread == "unpack")
    {
        return SfpuExecutionThread::Unpack;
    }
    if (sfpu_execution_thread == "math")
    {
        return SfpuExecutionThread::Math;
    }
    if (sfpu_execution_thread == "pack")
    {
        return SfpuExecutionThread::Pack;
    }
    return SfpuExecutionThread::Invalid;
}

SpliceMode netlist_utils::get_splice_mode(string splice_mode) {
    if (splice_mode == "ublock") {
        return SpliceMode::Ublock;
    } else if (splice_mode == "t") {
        return SpliceMode::T;
    } else {
        return SpliceMode::Invalid;
    }
}

bool netlist_utils::is_number(const std::string& s) {
    return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

int netlist_utils::get_variable_num(string input) {
    input.erase(0, 1);
    if (!is_number(input))
        log_fatal("variable string must have a postive number assignment -- eg. $1 $23");
    return stoi(input);
}

DataFormat netlist_utils::get_data_format(string format_string) {
    DataFormat df = DataFormat::Invalid;

    if (format_string == "Float32") {
        df = DataFormat::Float32;
    }
    if (format_string == "Float16") {
        df = DataFormat::Float16;
    }
    if (format_string == "Bfp8") {
        df = DataFormat::Bfp8;
    }
    if (format_string == "Bfp4") {
        df = DataFormat::Bfp4;
    }
    if (format_string == "Bfp2") {
        df = DataFormat::Bfp2;
    }
    if (format_string == "Float16_b") {
        df = DataFormat::Float16_b;
    }
    if (format_string == "Bfp8_b") {
        df = DataFormat::Bfp8_b;
    }
    if (format_string == "Bfp4_b") {
        df = DataFormat::Bfp4_b;
    }
    if (format_string == "Bfp2_b") {
        df = DataFormat::Bfp2_b;
    }
    if (format_string == "Lf8") {
        df = DataFormat::Lf8;
    }
    if (format_string == "UInt16") {
        df = DataFormat::UInt16;
    }
    if (format_string == "Int8") {
        df = DataFormat::Int8;
    }
    if (format_string == "Int32") {
        df = DataFormat::Int32;
    }
    if (format_string == "RawUInt8") {
        df = DataFormat::RawUInt8;
    }
    if (format_string == "RawUInt16") {
        df = DataFormat::RawUInt16;
    }
    if (format_string == "RawUInt32") {
        df = DataFormat::RawUInt32;
    }
    if (df == DataFormat::Invalid)
        log_fatal("Data Format not supported in netlist");
    return df;
}

MathFidelity netlist_utils::get_fidelity(string fidelity_string) {
    MathFidelity mf = MathFidelity::Invalid;

    if (fidelity_string == "LoFi") {
        mf = MathFidelity::LoFi;
    }
    if (fidelity_string == "HiFi2") {
        mf = MathFidelity::HiFi2;
    }
    if (fidelity_string == "HiFi3") {
        mf = MathFidelity::HiFi3;
    }
    if (fidelity_string == "HiFi4") {
        mf = MathFidelity::HiFi4;
    }
    if (fidelity_string == "HiFi2_A") {
        mf = MathFidelity::HiFi2_A;
    }
    if (fidelity_string == "HiFi2_B") {
        mf = MathFidelity::HiFi2_B;
    }
    if (fidelity_string == "Invalid") {
        mf = MathFidelity::Invalid;
    }
    if (mf == MathFidelity::Invalid)
        log_fatal("Math Fidelity not supported in netlist");
    return mf;
}

BinaryOp netlist_utils::get_binary_op(string binary_op) {
    BinaryOp op = BinaryOp::Invalid;
    if (binary_op == "add") {
        op = BinaryOp::Add;
    }
    if (binary_op == "subtract") {
        op = BinaryOp::Subtract;
    }
    if (binary_op == "multiply") {
        op = BinaryOp::Multiply;
    }
    if (binary_op == "quantization") {
        op = BinaryOp::Quantization;
    }
    if (binary_op == "dequantization") {
        op = BinaryOp::Dequantization;
    }
    if (binary_op == "requantization") {
        op = BinaryOp::Requantization;
    }
    return op;
}

string netlist_utils::binary_op_to_string(BinaryOp binary_op) {
    switch (binary_op) {
        case BinaryOp::Add:
            return "Add";
        case BinaryOp::AddInt32:
            return "AddInt32";
        case BinaryOp::Subtract:
            return "Subtract";
        case BinaryOp::Multiply:
            return "Multiply";
        case BinaryOp::Quantization:
            return "Quantization";
        case BinaryOp::Dequantization:
            return "Dequantization";
        case BinaryOp::Requantization:
            return "Requantization";
        default:
            log_fatal("Invalid binary op in netlist");
            return "";
    }
}

UnaryOp netlist_utils::get_unary_op(string unary_op) {
    UnaryOp op = UnaryOp::Invalid;

    if ((unary_op == "datacopy") || (unary_op == "nop")) {
        op = UnaryOp::Datacopy;
    } else if (netlist_utils::is_valid_ethernet_op(unary_op)) {
        op = UnaryOp::Datacopy;
    }

    return op;
}

NaryOp netlist_utils::get_nary_op(string nary_op) {
    NaryOp op = NaryOp::Invalid;
    if (nary_op == "splice") {
        op = NaryOp::Splice;
    }
    return op;
}

TmOp netlist_utils::get_tm_op(string tm_op) {
    TmOp op = TmOp::Invalid;
    if (tm_op == "r_broadcast") {
        op = TmOp::rBroadcast;
    } else if (tm_op == "c_broadcast") {
        op = TmOp::cBroadcast;
    } else if (tm_op == "z_broadcast") {
        op = TmOp::zBroadcast;
    } else if (tm_op == "hslice") {
        op = TmOp::hSlice;
    } else if (tm_op == "hstack") {
        op = TmOp::hStack;
    } else if (tm_op == "transpose") {
        op = TmOp::Transpose;
    } else if (tm_op == "vslice") {
        op = TmOp::vSlice;
    } else if (tm_op == "vstack") {
        op = TmOp::vStack;
    } else if (tm_op == "tile_broadcast") {
        op = TmOp::TileBroadcast;
    }
    return op;
}

SfpuOp netlist_utils::get_sfpu_op(string sfpu_op) {
    SfpuOp op = SfpuOp::Invalid;
    if (sfpu_op == "exp") {
        op = SfpuOp::Exp;
    } else if (sfpu_op == "log") {
        op = SfpuOp::Log;
    } else if (sfpu_op == "sqrt") {
        op = SfpuOp::Sqrt;
    } else if (sfpu_op == "gelu") {
        op = SfpuOp::Gelu;
    } else if (sfpu_op == "gelu_derivative") {
        op = SfpuOp::GeluDerivative;
    } else if (sfpu_op == "reciprocal") {
        op = SfpuOp::Reciprocal;
    } else if (sfpu_op == "sigmoid") {
        op = SfpuOp::Sigmoid;
    } else if (sfpu_op == "dropout") {
        op = SfpuOp::Dropout;
    } else if (sfpu_op == "tanh") {
        op = SfpuOp::Tanh;
    } else if (sfpu_op == "square") {
        op = SfpuOp::Square;
    } else if (sfpu_op == "power") {
        op = SfpuOp::Power;
    } else if (sfpu_op == "sine") {
        op = SfpuOp::Sine;
    } else if (sfpu_op == "cosine") {
        op = SfpuOp::Cosine;
    } else if (sfpu_op == "lrelu") {
        op = SfpuOp::LRelu;
    } else if (sfpu_op == "abs") {
        op = SfpuOp::Abs;
    }
    return op;
}

string netlist_utils::sfpu_op_to_string(SfpuOp sfpu_op) {
    switch (sfpu_op) {
        case SfpuOp::Abs: return "Abs";
        case SfpuOp::CastFp32ToFp16a: return "CastFp32ToFp16a";
        case SfpuOp::Cosine: return "Cosine";
        case SfpuOp::Dropout: return "Dropout";
        case SfpuOp::Exp: return "Exp";
        case SfpuOp::Gelu: return "Gelu";
        case SfpuOp::GeluDerivative: return "GeluDerivative";
        case SfpuOp::Log: return "Log";
        case SfpuOp::LRelu: return "LRelu";
        case SfpuOp::Max: return "Max";
        case SfpuOp::Power: return "Power";
        case SfpuOp::Reciprocal: return "Reciprocal";
        case SfpuOp::ReluMax: return "ReluMax";
        case SfpuOp::ReluMin: return "ReluMin";
        case SfpuOp::Sigmoid: return "Sigmoid";
        case SfpuOp::Sine: return "Sine";
        case SfpuOp::Sqrt: return "Sqrt";
        case SfpuOp::Square: return "Square";
        case SfpuOp::Tanh: return "Tanh";
        case SfpuOp::Invalid:
        default: break;
    };
    log_assert(false, "Invalid Sfpu Op: {}", sfpu_op);
    return "Invalid";
}

EmbeddingOp netlist_utils::get_embedding_op(string embedding_op) {
    EmbeddingOp op = EmbeddingOp::Invalid;
    if (embedding_op == "embedding") {
        op = EmbeddingOp::Embedding;
    }
    return op;
}
TilizerOp netlist_utils::get_tilizer_op(string tilizer_op) {
    TilizerOp op = TilizerOp::Invalid;
    if (tilizer_op == "tilizer") {
        op = TilizerOp::Tilizer;
    }
    return op;
}

DepthwiseOp netlist_utils::get_depthwise_op(string depthwise_op) {
    DepthwiseOp op = DepthwiseOp::Invalid;
    if (depthwise_op == "depthwise") {
        op = DepthwiseOp::Matmul;
    }
    return op;
}

ReduceFunc netlist_utils::get_reduce_func(string reduce_func) {
    ReduceFunc func = ReduceFunc::Invalid;
    if (reduce_func == "max") {
        func = ReduceFunc::Max;
    }
    // We only support max reduce for now

    // else if (reduce_func == "avg") {
    //     func = ReduceFunc::Avg;
    // } else if (reduce_func == "sum") {
    //     func = ReduceFunc::Sum;
    // }
    return func;
}
string netlist_utils::get_reduce_func_string(ReduceFunc reduce_func) {
    switch (reduce_func) {
        case ReduceFunc::Max: return "ReduceFunc::Max";
        case ReduceFunc::Avg: return "ReduceFunc::Avg";
        case ReduceFunc::Sum: return "ReduceFunc::Sum";
        default: return "ReduceFunc::Invalid";
    }
}
string netlist_utils::get_relu_mode_string(ReluMode relu_mode) {
    switch (relu_mode) {
        case ReluMode::Max: return "ReluMode::Max";
        case ReluMode::Min: return "ReluMode::Min";
        default: return "ReluMode::Invalid";
    }
}
string netlist_utils::get_splice_mode_string(SpliceMode splice_mode) {
    switch (splice_mode) {
        case SpliceMode::T: return "SpliceMode::T";
        case SpliceMode::Ublock: return "SpliceMode::Ublock";
        default: return "SpliceMode::Invalid";
    }
}

BinaryOp netlist_utils::get_valid_binary_op(string binary_op) {
    BinaryOp op = get_binary_op(binary_op);
    if (op == BinaryOp::Invalid)
        log_fatal("Binary Op={} not supported in netlist", binary_op);
    return op;
}

UnaryOp netlist_utils::get_valid_unary_op(string unary_op) {
    UnaryOp op = get_unary_op(unary_op);
    if (op == UnaryOp::Invalid)
        log_fatal("Unary Op={} not supported in netlist", unary_op);
    return op;
}

NaryOp netlist_utils::get_valid_nary_op(string nary_op) {
    NaryOp op = get_nary_op(nary_op);
    if (op == NaryOp::Invalid)
        log_fatal("Nary Op={} not supported in netlist", nary_op);
    return op;
}

TmOp netlist_utils::get_valid_tm_op(string tm_op) {
    TmOp op = get_tm_op(tm_op);
    if (op == TmOp::Invalid)
        log_fatal("Tm Op={} not supported in netlist", tm_op);
    return op;
}

SfpuOp netlist_utils::get_valid_sfpu_op(string sfpu_op) {
    SfpuOp op = get_sfpu_op(sfpu_op);
    if (op == SfpuOp::Invalid)
        log_fatal("Sfpu Op={} not supported in netlist", sfpu_op);
    return op;
}

EmbeddingOp netlist_utils::get_valid_embedding_op(string embedding_op) {
    EmbeddingOp op = get_embedding_op(embedding_op);
    if (op == EmbeddingOp::Invalid)
        log_fatal("Embedding Op={} not supported in netlist", embedding_op);
    return op;
}

DepthwiseOp netlist_utils::get_valid_depthwise_op(string depthwise_op) {
    DepthwiseOp op = get_depthwise_op(depthwise_op);
    if (op == DepthwiseOp::Invalid)
        log_fatal("Depthwise Op={} not supported in netlist", depthwise_op);
    return op;
}

bool netlist_utils::is_valid_matmul_op(string matmul_op) { return (matmul_op == "matmul"); }

ReduceFunc netlist_utils::get_valid_reduce_func(string reduce_func) {
    ReduceFunc func = get_reduce_func(reduce_func);
    if (func == ReduceFunc::Invalid)
        log_fatal("ReduceFunc={} not supported in netlist", reduce_func);
    return func;
}
SpliceMode netlist_utils::get_valid_splice_mode(string splice_mode) {
    SpliceMode mode = get_splice_mode(splice_mode);
    if (mode == SpliceMode::Invalid)
        log_fatal("SpliceMode={} not supported in netlist", splice_mode);
    return mode;
}

StochRndMode netlist_utils::get_stoch_rnd_mode(string srnd_mode_string) {
    if (srnd_mode_string == "none") { return StochRndMode::None; }
    else if (srnd_mode_string == "fpu") { return StochRndMode::Fpu; }
    else if (srnd_mode_string == "pack") { return StochRndMode::Pack; }
    else if (srnd_mode_string == "all") { return StochRndMode::All; }
    else {
        log_fatal("Stochastic rounding mode not supported", srnd_mode_string); 
        return  StochRndMode::Invalid;
    }
}

bool netlist_utils::is_valid_splice_op(string splice_op) { return (splice_op == "splice"); }
bool netlist_utils::is_valid_reduce_op(string reduce_op) { return (reduce_op == "reduce"); }
bool netlist_utils::is_valid_binary_op(string binary_op) {
    BinaryOp op = get_binary_op(binary_op);
    return (op != BinaryOp::Invalid);
}
bool netlist_utils::is_valid_binary_quantisation_op(string binary_op) {
    BinaryOp op = get_binary_op(binary_op);
    return (op == BinaryOp::Quantization || op == BinaryOp::Dequantization || op == BinaryOp::Requantization);
}
bool netlist_utils::is_valid_unary_op(string unary_op) {
    UnaryOp op = get_unary_op(unary_op);
    return (op != UnaryOp::Invalid);
}
bool netlist_utils::is_valid_nary_op(string nary_op) {
    NaryOp op = get_nary_op(nary_op);
    return (op != NaryOp::Invalid);
}
bool netlist_utils::is_valid_sfpu_op(string sfpu_op) {
    SfpuOp op = get_sfpu_op(sfpu_op);
    return (op != SfpuOp::Invalid);
}

bool netlist_utils::is_valid_embedding_op(string embedding_op) {
    EmbeddingOp op = get_embedding_op(embedding_op);
    return (op != EmbeddingOp::Invalid);
}

bool netlist_utils::is_valid_tilizer_op(string tilizer_op) {
    TilizerOp op = get_tilizer_op(tilizer_op);
    return (op != TilizerOp::Invalid);
}

bool netlist_utils::is_valid_depthwise_op(string depthwise_op) {
    DepthwiseOp op = get_depthwise_op(depthwise_op);
    return (op != DepthwiseOp::Invalid);
}

bool netlist_utils::is_valid_fused_op(string fused_op) { return (fused_op == "fused_op"); }
bool netlist_utils::is_valid_ethernet_op(string op_type) { return (op_type == "ethernet_datacopy"); }

bool netlist_utils::is_non_tensix_op(string op_type) { return is_valid_ethernet_op(op_type); }

std::uint32_t netlist_utils::conv_float_to_u16a(float in_f) {
    union {
        float f;
        uint32_t u32;
    } in = {.f = in_f};

    uint32_t m = in.u32 & 0x007fffff;
    uint32_t e = (in.u32 & 0x7F800000) >> 23;
    uint32_t s = (in.u32 & 0x80000000) >> 16;  // move to bit 16
    // unbias and rebias exponent, shift down mantissa
    // with saturation to bottom/top of range
    int se = (int)e;
    se = se - 127;
    // HW specific fp16_a has 5 bit exponent with no representation for inf
    // this can hold 0 -> 31, which represents -15 -> 16
    if (se <= (-15)) {
        se = -15;
        m = 0;
    } else if (se > 16) {
        se = 16;
        m = 0x007fffff >> 13;
    } else {
        se = se;
        m = m >> 13;
    }
    // add bias for fp16_a
    se += 15;
    log_assert(se >= 0, "expected positive exponent");
    e = (uint32_t)se;
    e = e << 10;
    uint32_t out = s | e | m;
    return (out);
}

std::uint32_t netlist_utils::conv_float_to_u16b(float in_f) {
    union {
        float f;
        uint32_t u32;
    } in = {.f = in_f};

    uint32_t out = in.u32 >> 16;
    return (out);
}

bool netlist_utils::is_exp_a_format(DataFormat df) {
    return df == DataFormat::Float16 || df == DataFormat::Bfp8 || df == DataFormat::Bfp4 || df == DataFormat::Bfp2;
}

bool netlist_utils::is_exp_b_format(DataFormat df) {
    return df == DataFormat::Float16_b || df == DataFormat::Bfp8_b || df == DataFormat::Bfp4_b ||
           df == DataFormat::Bfp2_b;
}

bool netlist_utils::is_bfp2_format(DataFormat df) { return (df == DataFormat::Bfp2 || df == DataFormat::Bfp2_b); }

bool netlist_utils::is_bfp4_format(DataFormat df) { return (df == DataFormat::Bfp4 || df == DataFormat::Bfp4_b); }

bool netlist_utils::is_bfp8_format(DataFormat df) { return (df == DataFormat::Bfp8 || df == DataFormat::Bfp8_b); }

bool netlist_utils::is_bfp_format(DataFormat df) {
    return is_bfp2_format(df) || is_bfp4_format(df) || is_bfp8_format(df);
}
bool netlist_utils::is_int_format(DataFormat df) {
    return (df == DataFormat::Int8 || df == DataFormat::Int32);

}
uint32_t netlist_utils::get_bit_width(uint32_t val) {
    int bit_width = 0;
    do {
        bit_width++;
    } while (((val - 1) >> bit_width) > 0);

    return bit_width;
}

// return number of bytes for each data format
float netlist_utils::get_data_format_size(DataFormat df) {
    switch (df) {
        case DataFormat::Float32: return 4;
        case DataFormat::Float16_b:
        case DataFormat::Float16: return 2;
        case DataFormat::Bfp8_b:
        case DataFormat::Bfp8: return 1;
        case DataFormat::Bfp4_b:
        case DataFormat::Bfp4: return 0.5;
        case DataFormat::Bfp2_b:
        case DataFormat::Bfp2: return 0.25;
        default: log_fatal("Dataformat={} is not recognized", df); return -1;
    }
}

TileDimReconfigMode netlist_utils::get_tile_dim_reconfig_mode(const tt_op_info& op_info) {
    
    TileDimReconfigMode tile_dim_reconfig_mode = {0,0};
    
    if(is_valid_matmul_op(op_info.type)) {

        //Check if unpacker needs tile dim reconfig
        if( (op_info.attributes.m_k>1 || op_info.attributes.bias) &&
            (op_info.input_tile_dims.at(1) != op_info.output_tile_dim)) {
                tile_dim_reconfig_mode.unpack_reconfig_en = 1;
        
        } else if(op_info.attributes.bias && 
            (op_info.input_tile_dims.at(0) != op_info.input_tile_dims.at(2))) {
                tile_dim_reconfig_mode.unpack_reconfig_en = 1;
        }

        // Check if packer needs tile dim reconfig, if one of the packer data formats is BFP
        // and both pack buffers are partial faces, mop needs to be reconfigured
        if( (op_info.attributes.m_k>1 || op_info.attributes.bias) &&
            (((is_bfp_format(op_info.intermed_data_format) ^ is_bfp_format(op_info.output_data_format)) &&
            op_info.output_tile_dim != TileDim::Dim32x32))) {
                tile_dim_reconfig_mode.pack_reconfig_en = 1; 
        } 
    } else if(is_valid_binary_op(op_info.type)) {
        if(op_info.gradient_op && (op_info.input_tile_dims.at(0) != op_info.output_tile_dim)) {
            tile_dim_reconfig_mode.unpack_reconfig_en = 1;
        }
    }
    return tile_dim_reconfig_mode;
}

std::shared_ptr<tt_op> netlist_utils::create_op(
    tt_op_info* op_info_ptr, const unordered_map<string, tt_fused_op_info>& fused_ops_map) {
    std::shared_ptr<tt_op> new_tt_op;
    log_debug(tt::LogNetlist, "create_op: {}", op_info_ptr->type);

    // Get common params
    std::uint32_t block_tile_dim = 0;
    std::uint32_t block_cnt = 0;
    std::uint32_t batch_cnt = op_info_ptr->output_dim.t;
    std::uint32_t num_m_sub_blocks = op_info_ptr->output_dim.mblock_m;
    std::uint32_t num_n_sub_blocks = op_info_ptr->output_dim.mblock_n;
    std::uint32_t num_tiles_per_m_sub_block = op_info_ptr->output_dim.ublock_rt;
    std::uint32_t num_tiles_per_n_sub_block = op_info_ptr->output_dim.ublock_ct;
    tt_grid_shape grid_shape;
    grid_shape.r = op_info_ptr->grid_size_logical_r();
    grid_shape.c = op_info_ptr->grid_size_logical_c();
    tt_grid_shape grid_loc;
    grid_loc.r = is_valid_ethernet_op(op_info_ptr->type) ? -1 : op_info_ptr->grid_loc_y();
    grid_loc.c = is_valid_ethernet_op(op_info_ptr->type) ? -1 : op_info_ptr->grid_loc_x();
    bool grid_transpose = op_info_ptr->grid_transpose;

    std::uint32_t gradient_op = op_info_ptr->gradient_op ? 1 : 0;
    std::uint32_t transpose = op_info_ptr->transpose ? 1 : 0;
    bool fp32_dest_acc_en = op_info_ptr->dest_accumulate_data_format == DataFormat::Float32;
    bool relu_en = op_info_ptr->attributes.relu_en;
    ReluMode relu_mode = op_info_ptr->attributes.relu_mode;

    std::uint32_t relu_threshold = relu_en ? (is_integer_format(op_info_ptr->output_data_format) ? (std::int32_t)op_info_ptr->attributes.relu_threshold : ((is_exp_a_format(op_info_ptr->output_data_format) && !fp32_dest_acc_en)
                                                  ? conv_float_to_u16a(op_info_ptr->attributes.relu_threshold)
                                                  : conv_float_to_u16b(op_info_ptr->attributes.relu_threshold)))
                                           : 0;

    std::vector<std::vector<int>> input_tile_dims;
    for (int i = 0; i < op_info_ptr->input_tile_dims.size(); i++) {
        input_tile_dims.push_back(tile_dim_to_array(op_info_ptr->input_tile_dims[i]));
    }

    std::vector<int> out_tile_dims = tile_dim_to_array(op_info_ptr->output_tile_dim);
    std::vector<int> default_tile_dims = {DEFAULT_TILE_DIMS[0], DEFAULT_TILE_DIMS[1]};

    // Decode op types
    if (is_valid_matmul_op(op_info_ptr->type)) {
        float zero_point = op_info_ptr->attributes.dequant ? -op_info_ptr->attributes.zero_point :  //dequant op adds zero_point to the result
                                                          op_info_ptr->attributes.zero_point; 
        uint32_t block_tile_dim_bits = get_bit_width(op_info_ptr->attributes.u_kt);

        uint32_t sparse_ublock_idx_bits = (std::uint32_t)(
            op_info_ptr->attributes.sparse_ublock_idx_bits <= 0 ? op_info_ptr->attributes.sparse_tile_ptr_bits
                                                                : op_info_ptr->attributes.sparse_ublock_idx_bits);

        vector<TileDim> tile_dims = op_info_ptr->input_tile_dims;

        TileDimReconfigMode tile_dim_reconfig_mode = get_tile_dim_reconfig_mode(*op_info_ptr);    

        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_mm_bare_op>(
            op_info_ptr->name,                              // string name
            op_info_ptr->type,                              // string type
            grid_shape,                                     // tt_grid_shape grid_shape
            grid_loc,                                       // tt_grid_shape grid_loc
            grid_transpose,                                 // bool grid_transpose
            op_info_ptr->attributes.u_kt,                   // std::uint32_t block_tile_dim
            op_info_ptr->attributes.m_k,                    // std::uint32_t block_cnt
            batch_cnt,                                      // std::uint32_t batch_cnt
            num_m_sub_blocks,                               // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                               // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,                      // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,                      // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                                    // std::uint32_t gradient_op
            transpose,                                      // std::uint32_t transpose
            op_info_ptr->untilize_output,                   // bool untilize_output
            op_info_ptr->math_fidelity,                     // MathFidelity math_fidelity
            fp32_dest_acc_en,                               // bool fp32_dest_acc_en
            relu_en,                                        // bool relu_en
            relu_threshold,                                 // std::uint32_t relu_threshold
            relu_mode,                                      // ReluMode relu_mode
            op_info_ptr->input_data_formats,                // Input DataFormat 
            op_info_ptr->output_data_format,                // DataFormat out_data_format
            op_info_ptr->intermed_data_format,              // DataFormat intermed_data_format
            op_info_ptr->attributes.identity,               // Bool identity matmul
            op_info_ptr->attributes.num_index_tiles,
            op_info_ptr->attributes.num_sparse_tiles,
            op_info_ptr->attributes.num_columns,
            op_info_ptr->attributes.sparse_tile_ptr_bits,
            sparse_ublock_idx_bits,
            block_tile_dim_bits,
            op_info_ptr->attributes.indices_len,
            op_info_ptr->attributes.fracture_factor,
            op_info_ptr->attributes.starting_row,
            op_info_ptr->attributes.bias,
            op_info_ptr->attributes.accumulate,
            op_info_ptr->attributes.z,
            op_info_ptr->attributes.min_input_buffer,
            op_info_ptr->attributes.l1_acc,
            op_info_ptr->attributes.kernel_broadcast,            // kernel_broadcast
            op_info_ptr->attributes.sfpu_op,                // sfpu_op
            op_info_ptr->attributes.vector_mode,            // sfpu vector mode
            op_info_ptr->attributes.sfpu_execution_thread,   // sfpu execution thread
            op_info_ptr->attributes.act_t == 0 ? op_info_ptr->output_dim.t : op_info_ptr->attributes.act_t,  //post tm activation t for identity matmul 
                                                                                                             //if not set, input t == output t
            op_info_ptr->arch_name,
            op_info_ptr->attributes.stoch_rnd_mode,
            op_info_ptr->attributes.requant,
            op_info_ptr->attributes.dequant,
            zero_point,
            input_tile_dims,
            out_tile_dims,
            tile_dim_reconfig_mode));

    } else if (is_valid_sfpu_op(op_info_ptr->type)) {
        // Process any inputs before driving unary op
        SfpuOp sfpu_op = get_valid_sfpu_op(op_info_ptr->type);
        bool approximate_mode = op_info_ptr->attributes.approximate_mode;
        std::uint32_t scale_u16b = conv_float_to_u16b(op_info_ptr->attributes.scale);
        std::uint32_t slope_u16b = conv_float_to_u16b(op_info_ptr->attributes.slope);

        log_assert(input_tile_dims.size() == 1, "Expected 1 input in input_tile_dims array on sfpu op, but got {}", input_tile_dims.size());

        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_eltwise_unary_sfpu_bare_op>(
            op_info_ptr->name,                              // string name
            op_info_ptr->type,                              // string type
            grid_shape,                                     // tt_grid_shape grid_shape
            grid_loc,                                       // tt_grid_shape grid_loc
            grid_transpose,                                 // bool grid_transpose
            sfpu_op,
            approximate_mode,
            block_tile_dim,                                 // std::uint32_t block_tile_dim
            block_cnt,                                      // std::uint32_t block_cnt
            batch_cnt,                                      // std::uint32_t batch_cnt
            num_m_sub_blocks,                               // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                               // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,                      // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,                      // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                                    // std::uint32_t gradient_op
            transpose,
            op_info_ptr->untilize_output,                   // bool untilize_output
            op_info_ptr->math_fidelity,                     // MathFidelity math_fidelity
            fp32_dest_acc_en,                               // bool fp32_dest_acc_en
            relu_en,                                        // bool relu_en
            relu_threshold,                                 // std::uint32_t relu_threshold
            relu_mode,                                      // ReluMode relu_mode
            op_info_ptr->attributes.probability,            // float probability
            op_info_ptr->attributes.scale,                  // float scale
            scale_u16b,                                     // std::uint32_t probability
            op_info_ptr->attributes.seed,                   // std::uint32_t seed
            op_info_ptr->attributes.exponent,               // std::int32_t seed
            op_info_ptr->attributes.slope,                  // float slope
            slope_u16b,                                     // std::uint32_t slope
            op_info_ptr->input_data_formats[0],             // DataFormat in_0_data_format
            op_info_ptr->output_data_format,                // DataFormat out_data_format
            op_info_ptr->intermed_data_format,              // DataFormat intermed_data_format
            op_info_ptr->attributes.vector_mode,            // vector_mode
            op_info_ptr->attributes.sfpu_execution_thread,  // Sfpu execution thread
            op_info_ptr->attributes.kernel_broadcast,       // kernel_broadcast
            input_tile_dims[0],                             // input tile dims (used for datacopy only atm)
            out_tile_dims,                                  // output tile dims (used for datacopy only atm)
            op_info_ptr->attributes.stoch_rnd_mode
            ));
    } else if (is_valid_reduce_op(op_info_ptr->type)) {
        // Process any inputs before driving reduce op
        ReduceFunc reduce_type = op_info_ptr->attributes.reduce_type;
        Dim reduce_dim = op_info_ptr->attributes.reduce_dim;
        std::uint32_t block_tile_dim = op_info_ptr->attributes.u_kt;
        std::uint32_t block_cnt = op_info_ptr->attributes.m_k;

        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_reduce_bare_op>(
            op_info_ptr->name,  // string name
            op_info_ptr->type,  // string type
            grid_shape,         // tt_grid_shape grid_shape
            grid_loc,           // tt_grid_shape grid_loc
            grid_transpose,     // bool grid_transpose
            reduce_type,
            reduce_dim,
            block_tile_dim,             // std::uint32_t block_tile_dim
            block_cnt,                  // std::uint32_t block_cnt
            batch_cnt,                  // std::uint32_t batch_cnt
            num_m_sub_blocks,           // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,           // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,  // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,  // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                // std::uint32_t gradient_op
            transpose,
            op_info_ptr->attributes.z,           // std::uint32 z_dim
            op_info_ptr->untilize_output,        // bool untilize_output
            op_info_ptr->math_fidelity,          // MathFidelity math_fidelity
            fp32_dest_acc_en,                    // bool fp32_dest_acc_en
            relu_en,                             // bool relu_en
            relu_threshold,                      // std::uint32_t relu_threshold
            relu_mode,                           // ReluMode relu_mode
            op_info_ptr->input_data_formats[0],  // DataFormat in_0_data_format
            op_info_ptr->output_data_format,     // DataFormat out_data_format
            op_info_ptr->intermed_data_format,    // DataFormat intermed_data_format
            input_tile_dims[0],
            out_tile_dims,
            op_info_ptr->attributes.stoch_rnd_mode
            ));
    } else if (is_valid_binary_op(op_info_ptr->type)) {
        // Process any inputs before driving binary op
        BinaryOp binary_op = get_valid_binary_op(op_info_ptr->type);
        // dequant op adds zero_point to the result
        float zero_point = binary_op == BinaryOp::Dequantization ? -op_info_ptr->attributes.zero_point : op_info_ptr->attributes.zero_point;

        TileDimReconfigMode tile_dim_reconfig_mode = get_tile_dim_reconfig_mode(*op_info_ptr); 

        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_eltwise_binary_bare_op>(
            op_info_ptr->name,  // string name
            op_info_ptr->type,  // string type
            grid_shape,         // tt_grid_shape grid_shape
            grid_loc,           // tt_grid_shape grid_loc
            grid_transpose,     // bool grid_transpose
            binary_op,
            block_tile_dim,             // std::uint32_t block_tile_dim
            block_cnt,                  // std::uint32_t block_cnt
            batch_cnt,                  // std::uint32_t batch_cnt
            num_m_sub_blocks,           // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,           // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,  // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,  // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                // std::uint32_t gradient_op
            transpose,
            op_info_ptr->untilize_output,              // bool untilize_output
            op_info_ptr->math_fidelity,                // MathFidelity math_fidelity
            fp32_dest_acc_en,                          // bool fp32_dest_acc_en
            relu_en,                                   // bool relu_en
            relu_threshold,                            // std::uint32_t relu_threshold
            relu_mode,                                 // ReluMode relu_mode
            op_info_ptr->input_data_formats[0],        // DataFormat in_0_data_format
            op_info_ptr->input_data_formats[1],        // DataFormat in_1_data_format
            op_info_ptr->output_data_format,           // DataFormat out_data_format
            op_info_ptr->intermed_data_format,         // DataFormat intermed_data_format
            op_info_ptr->attributes.kernel_broadcast,  // kernel_broadcast
            op_info_ptr->attributes.stoch_rnd_mode,
            zero_point,
            input_tile_dims,
            out_tile_dims,
            tile_dim_reconfig_mode));
    } else if (is_valid_nary_op(op_info_ptr->type)) {
        // Process any inputs before driving nary op
        NaryOp nary_op = get_valid_nary_op(op_info_ptr->type);
        block_tile_dim = op_info_ptr->output_dim.ublock_rt * op_info_ptr->output_dim.ublock_ct;

        std::vector<std::vector<int>> splice_configs = {};
        std::vector<tt::tt_block_shape> input_mblock_shapes = {};
        std::vector<tt::tt_block_shape> input_ublock_shapes = {};
        std::vector<Dim> input_ublock_orders = {};
        for (const auto& dims : op_info_ptr->input_dims) {
            input_mblock_shapes.push_back(
                {.r = static_cast<uint32_t>(dims.mblock_m), .c = static_cast<uint32_t>(dims.mblock_n)});
            input_ublock_shapes.push_back(
                {.r = static_cast<uint32_t>(dims.ublock_rt), .c = static_cast<uint32_t>(dims.ublock_ct)});
            input_ublock_orders.push_back(dims.ublock_order);
        }
        for (const auto& splice_info : op_info_ptr->attributes.splice_infos) {
            splice_configs.push_back({
                splice_info.index,
                splice_info.length,
                splice_info.stride,
            });
        }
        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_nary_bare_op>(
            op_info_ptr->name,  // string name
            op_info_ptr->type,  // string type
            grid_shape,         // tt_grid_shape grid_shape
            grid_loc,           // tt_grid_shape grid_loc
            grid_transpose,     // bool grid_transpose
            nary_op,
            block_tile_dim,                        // std::uint32_t block_tile_dim
            block_cnt,                             // std::uint32_t block_cnt
            batch_cnt,                             // std::uint32_t batch_cnt
            num_m_sub_blocks,                      // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                      // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,             // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,             // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                           // std::uint32_t gradient_op
            op_info_ptr->untilize_output,          // bool untilize_output
            op_info_ptr->math_fidelity,            // MathFidelity math_fidelity
            fp32_dest_acc_en,                      // bool fp32_dest_acc_en
            relu_en,                               // bool relu_en
            relu_threshold,                        // std::uint32_t relu_threshold
            relu_mode,                             // ReluMode relu_mode
            op_info_ptr->input_data_formats,       // in_data_formats
            op_info_ptr->output_data_format,       // out_data_format
            op_info_ptr->intermed_data_format,     // intermed_data_format
            op_info_ptr->output_dim.ublock_order,  // output_dim.ublock_order
            op_info_ptr->attributes.splice_mode,   // Splice Mode
            splice_configs,                        // splice_configs
            op_info_ptr->attributes.kernel_broadcast,    // kernel_broadcast
            op_info_ptr->attributes.stoch_rnd_mode,
            input_tile_dims,
            out_tile_dims
            ));
    } else if (is_valid_fused_op(op_info_ptr->type)) {
        // Need to pass matmul params in case matmul (for reduce) is fused
        std::uint32_t block_cnt = op_info_ptr->attributes.m_k;
        std::uint32_t block_tile_dim = op_info_ptr->attributes.u_kt;

        const std::string fused_op_id = op_info_ptr->name + "_" + op_info_ptr->attributes.fused_op_id;

        log_assert(
            fused_op_id != "",
            "fused_op_id={} for op={} cannot be empty",
            fused_op_id,
            op_info_ptr->name);
        log_assert(
            fused_ops_map.find(fused_op_id) != fused_ops_map.end(),
            "Cannot find fused_op_id={} in fused_op_map parsed from netlist",
            fused_op_id);


        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_fused_op>(
            op_info_ptr->name,                         // string name
            op_info_ptr->type,                         // string type
            grid_shape,                                // tt_grid_shape grid_shape
            grid_loc,                                  // tt_grid_shape grid_loc
            grid_transpose,                            // bool grid_transpose
            op_info_ptr->attributes.approximate_mode,  // bool approximate_mode
            block_tile_dim,                            // std::uint32_t block_tile_dim
            block_cnt,                                 // std::uint32_t block_cnt
            batch_cnt,                                 // std::uint32_t batch_cnt
            num_m_sub_blocks,                          // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                          // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,                 // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,                 // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                               // std::uint32_t gradient_op
            op_info_ptr->untilize_output,              // bool untilize_output
            op_info_ptr->math_fidelity,                // MathFidelity math_fidelity
            fp32_dest_acc_en,                          // bool fp32_dest_acc_en
            fused_op_id,                               // string id with hash
            op_info_ptr->input_data_formats,           // DataFormat in_data_formats
            op_info_ptr->output_data_format,           // DataFormat out_data_format
            op_info_ptr->intermed_data_format,         // DataFormat intermed_data_format,
            op_info_ptr->attributes.kernel_broadcast,  // kernel_broadcast
            fused_ops_map.at(fused_op_id),
            get_list_of_tt_op_info_from_fused_op(*op_info_ptr, fused_ops_map),
            input_tile_dims,
            out_tile_dims,
            op_info_ptr->attributes.stoch_rnd_mode));
        get_map_of_max_dims_per_intermed(fused_ops_map.at(fused_op_id));
        get_map_of_consumer_ops_per_input(*op_info_ptr, fused_ops_map);
        get_map_of_tile_broadcasting_per_input(fused_ops_map.at(fused_op_id));
    } else if (is_valid_embedding_op(op_info_ptr->type)) {
        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_embedding_op>(
            op_info_ptr->name,                   // string name
            op_info_ptr->type,                   // string type
            grid_shape,                          // tt_grid_shape grid_shape
            grid_loc,                            // tt_grid_shape grid_loc
            grid_transpose,                      // bool grid_transpose
            block_tile_dim,                      // std::uint32_t block_tile_dim
            block_cnt,                           // std::uint32_t block_cnt
            batch_cnt,                           // std::uint32_t batch_cnt
            num_m_sub_blocks,                    // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                    // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,           // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,           // std::uint32_t num_tiles_per_n_sub_block
            op_info_ptr->untilize_output,        // bool untilize_output
            op_info_ptr->math_fidelity,          // MathFidelity math_fidelity
            fp32_dest_acc_en,                    // bool fp32_dest_acc_en
            relu_en,                             // bool relu_en
            relu_threshold,                      // std::uint32_t relu_threshold
            relu_mode,                           // ReluMode relu_mode
            op_info_ptr->input_data_formats[0],  // DataFormat in_0_data_format
            op_info_ptr->input_data_formats[1],  // DataFormat in_1_data_format
            op_info_ptr->output_data_format,     // DataFormat out_data_format
            op_info_ptr->intermed_data_format,   // DataFormat intermed_data_format
            op_info_ptr->attributes.num_indices, // num_indices
            op_info_ptr->input_dims.at(0).mblock_n, // std::uint32 in_num_n_sub_blocks - embedding table mblock_n
            op_info_ptr->input_dims.at(0).ublock_ct, // std::uint32 in_tiles_per_num_n_sub_block - embedding table ublock_ct,
            input_tile_dims,
            out_tile_dims,
            op_info_ptr->attributes.stoch_rnd_mode
            ));
    } else if (is_valid_tilizer_op(op_info_ptr->type)) {
        uint32_t in_num_n_sub_blocks =  (op_info_ptr->input_dims.at(0).mblock_n*op_info_ptr->input_core_grids.at(0).c)/op_info_ptr->grid_size_logical_c();
        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_tilizer_op>(
            op_info_ptr->name,                   // string name
            op_info_ptr->type,                   // string type
            grid_shape,                          // tt_grid_shape grid_shape
            grid_loc,                            // tt_grid_shape grid_loc
            grid_transpose,                      // bool grid_transpose
            block_tile_dim,                      // std::uint32_t block_tile_dim
            block_cnt,                           // std::uint32_t block_cnt
            batch_cnt,                           // std::uint32_t batch_cnt
            num_m_sub_blocks,                    // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                    // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,           // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,           // std::uint32_t num_tiles_per_n_sub_block
            op_info_ptr->untilize_output,        // bool untilize_output
            op_info_ptr->math_fidelity,          // MathFidelity math_fidelity
            fp32_dest_acc_en,                    // bool fp32_dest_acc_en
            relu_en,                             // bool relu_en
            relu_threshold,                      // std::uint32_t relu_threshold
            relu_mode,                           // ReluMode relu_mode
            op_info_ptr->input_data_formats[0],  // DataFormat in_0_data_format
            op_info_ptr->output_data_format,     // DataFormat out_data_format
            op_info_ptr->intermed_data_format,   // DataFormat intermed_data_format
            in_num_n_sub_blocks,                 // std::uint32 in_num_n_sub_blocks - flat input mblock_n per core
            op_info_ptr->input_dims.at(0).ublock_ct, // std::uint32 in_tiles_per_num_n_sub_block - flat input table ublock_ct,
            input_tile_dims[0],
            out_tile_dims,
            op_info_ptr->attributes.stoch_rnd_mode
            ));
    } else if (is_valid_depthwise_op(op_info_ptr->type)) {
        DepthwiseOp depthwise_op = netlist_utils::get_depthwise_op(op_info_ptr->type);
        std::uint32_t block_cnt = op_info_ptr->attributes.m_k;
        std::uint32_t block_tile_dim = op_info_ptr->attributes.u_kt;
        log_assert(
            block_tile_dim == 1,
            "u_kt must be 1 for depthwise op {}",
            op_info_ptr->name);

        DataFormat input2_data_format = op_info_ptr->attributes.bias ?
                op_info_ptr->input_data_formats[2] : DataFormat::Invalid;

        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_depthwise_op>(
            op_info_ptr->name,                   // string name
            op_info_ptr->type,                   // string type
            grid_shape,                          // tt_grid_shape grid_shape
            grid_loc,                            // tt_grid_shape grid_loc
            grid_transpose,                      // bool grid_transpose
            depthwise_op,                        // DepthwiseOpType depthwise_op_type
            batch_cnt,                           // std::uint32_t batch_cnt
            block_tile_dim,
            num_m_sub_blocks,                    // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                    // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,           // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,           // std::uint32_t num_tiles_per_n_sub_block
            block_cnt,
            gradient_op,                         // std::uint32_t gradient_op
            transpose,                           // std::uint32_t transpose
            op_info_ptr->untilize_output,        // bool untilize_output
            op_info_ptr->math_fidelity,          // MathFidelity math_fidelity
            fp32_dest_acc_en,                    // bool fp32_dest_acc_en
            relu_en,                             // bool relu_en
            relu_threshold,                      // std::uint32_t relu_threshold
            relu_mode,                           // ReluMode relu_mode
            op_info_ptr->input_data_formats[0],  // DataFormat in_0_data_format
            op_info_ptr->input_data_formats[1],  // DataFormat in_1_data_format
            input2_data_format,                  // DataFormat in_2_data_format - for bias
            op_info_ptr->output_data_format,     // DataFormat out_data_format
            op_info_ptr->intermed_data_format,   // DataFormat intermed_data_format
            op_info_ptr->attributes.bias,        // bool bias_en
            op_info_ptr->attributes.l1_acc,
            op_info_ptr->attributes.min_input_buffer,
            op_info_ptr->attributes.kernel_broadcast,   // kernel_broadcast
            op_info_ptr->arch_name,
            op_info_ptr->attributes.stoch_rnd_mode,
            input_tile_dims,
            out_tile_dims
            ));
    } else if (is_valid_unary_op(op_info_ptr->type)) {

        log_assert(input_tile_dims.size() == 1, "Expected 1 input in input_tile_dims array on unary op, but got {}", input_tile_dims.size());

        UnaryOp unary_op = get_valid_unary_op(op_info_ptr->type);
        new_tt_op = std::static_pointer_cast<tt_op>(std::make_shared<tt_unary_bare_op>(
            op_info_ptr->name,                          // string name
            op_info_ptr->type,                          // string type
            grid_shape,                                 // tt_grid_shape grid_shape
            grid_loc,                                   // tt_grid_shape grid_loc
            grid_transpose,                             // bool grid_transpose
            unary_op,                                   // UnaryOp
            block_tile_dim,                             // std::uint32_t block_tile_dim
            block_cnt,                                  // std::uint32_t block_cnt
            batch_cnt,                                  // std::uint32_t batch_cnt
            num_m_sub_blocks,                           // std::uint32_t num_m_sub_blocks
            num_n_sub_blocks,                           // std::uint32_t num_n_sub_blocks
            num_tiles_per_m_sub_block,                  // std::uint32_t num_tiles_per_m_sub_block
            num_tiles_per_n_sub_block,                  // std::uint32_t num_tiles_per_n_sub_block
            gradient_op,                                // std::uint32_t gradient_op
            transpose,                                  // std::uint32_t transpose
            op_info_ptr->untilize_output,               // bool untilize_output
            op_info_ptr->math_fidelity,                 // MathFidelity math_fidelity
            fp32_dest_acc_en,                           // bool fp32_dest_acc_en
            relu_en,                                    // bool relu_en
            relu_threshold,                             // std::uint32_t relu_threshold
            relu_mode,                                  // ReluMode relu_mode
            op_info_ptr->input_data_formats,            // in_data_formats
            op_info_ptr->output_data_format,            // out_data_format
            op_info_ptr->intermed_data_format,          // intermed_data_format
            op_info_ptr->attributes.kernel_broadcast,   // kernel_broadcast,
            input_tile_dims[0],                         // input tile dims (used for datacopy only atm)
            out_tile_dims,                              // output tile dims (used for datacopy only atm)
            op_info_ptr->attributes.stoch_rnd_mode
        ));
    } else {
        log_fatal("Internal Error -- Op type parsed is not supported in backend {}", op_info_ptr->type);
    }
    return (new_tt_op);
}

bool netlist_utils::is_queueless_multichip_supported(const tt::ARCH& device) {
    switch (device) {
        case tt::ARCH::GRAYSKULL: return true;
        case tt::ARCH::WORMHOLE: return true;
        case tt::ARCH::WORMHOLE_B0: return true;
        default: return false;
    }
}

std::unordered_map<std::string, tt_dim_info> netlist_utils::get_map_of_max_dims_per_intermed(
    const tt_fused_op_info& fused_op_info) {
    std::unordered_map<std::string, tt_dim_info> results = {};
    for (const auto& schedule : fused_op_info.schedules) {
        for (const auto& scheduled_op : schedule.scheduled_ops) {
            if (scheduled_op.output.find("intermed") == 0) {
                // Intermediate output -- Check if ublock size is bigger
                if (results.find(scheduled_op.output) == results.end()) {
                    results.insert({scheduled_op.output, scheduled_op.output_dim});
                } else if (
                    scheduled_op.output_dim.ublock_ct * scheduled_op.output_dim.ublock_rt >
                    results.at(scheduled_op.output).ublock_ct * results.at(scheduled_op.output).ublock_rt) {
                    results.at(scheduled_op.output) = scheduled_op.output_dim;
                }
            }
        }
    }
    return results;
}

std::unordered_map<std::string, bool> netlist_utils::get_map_of_tile_broadcasting_per_input(
    const tt_fused_op_info& fused_op_info) {
    std::unordered_map<std::string, bool> results = {};
    for (const auto& schedule : fused_op_info.schedules) {
        for (const auto& scheduled_op : schedule.scheduled_ops) {
            for (const auto& scheduled_op_input_name : scheduled_op.input_names) {
                if (scheduled_op_input_name != "dest") {
                    int relative_index =
                        stoi(scheduled_op_input_name.substr(scheduled_op_input_name.find_first_of("0123456789")));
                    bool tile_broadcasted = false;
                    if (scheduled_op.input_tm_ops.find(relative_index) != scheduled_op.input_tm_ops.end()) {
                        for (const auto& tm_it : scheduled_op.input_tm_ops.at(relative_index)) {
                            tile_broadcasted = (TmOp::TileBroadcast == get_tm_op(tm_it.first));
                        }
                    }
                    results.insert({scheduled_op_input_name, tile_broadcasted});
                }
            }
        }
    }
    return results;
}

std::unordered_map<std::string, tt_scheduled_op_info> netlist_utils::get_map_of_consumer_ops_per_input(
    const tt_op_info& base_op_info, const unordered_map<string, tt_fused_op_info>& fused_ops_map) {
    std::unordered_map<std::string, tt_scheduled_op_info> consumer_ops_per_input = {};
    if (base_op_info.type == "fused_op") {
        const std::string& fused_op_id = base_op_info.attributes.fused_op_id;
        const auto& fused_op_info = fused_ops_map.at(base_op_info.name+ "_" + fused_op_id);
        std::vector<tt_op_info> result_op_infos;
        for (const auto& schedule : fused_op_info.schedules) {
            for (const auto& scheduled_op : schedule.scheduled_ops) {
                for (const auto& scheduled_op_input_name : scheduled_op.input_names) {
                    if (scheduled_op_input_name.find("input") != std::string::npos) {
                        if (consumer_ops_per_input.find(scheduled_op_input_name) == consumer_ops_per_input.end()) {
                            // First time the input is being used, we can add the information to map.
                            consumer_ops_per_input.insert({scheduled_op_input_name, scheduled_op});
                        }
                    }
                }
            }
        }

        log_assert(
            consumer_ops_per_input.size() == fused_op_info.num_inputs,
            "num_inputs={} specified in fused_op_id={} and num_inputs={} used in assosciated scheduled_op_info found "
            "mismatch",
            fused_op_info.num_inputs,
            fused_op_id,
            consumer_ops_per_input.size());
    }
    return consumer_ops_per_input;
}

void netlist_utils::update_fields_for_op_info_from_scheduled_op_info(
    const tt_op_info& base_op_info,
    const tt_scheduled_op_info& scheduled_op,
    const tt_fused_op_info& fused_op_info,
    tt_op_info& new_op_info) {
    // Modify type
    new_op_info.type = scheduled_op.type;
    // Modify uniquify name of op if it isn't the final output
    new_op_info.name = "fused_op." + fused_op_info.name + "." + scheduled_op.name;
    // Save output to attributes
    new_op_info.attributes.fused_op_output = scheduled_op.output;
    // Save inputs_to_pop to attributes
    new_op_info.attributes.fused_op_inputs_to_pop = scheduled_op.inputs_to_pop;
    // Save inputs_to_pop_last to attributes
    new_op_info.attributes.fused_op_inputs_to_pop_last = scheduled_op.inputs_to_pop_last;
    // Modify Input names
    new_op_info.input_names.clear();
    new_op_info.input_data_formats.clear();
    // Get input indices
    for (const auto& input : scheduled_op.input_names) {
        if (input.find("input") == 0) {
            // Input found - get indice and map it to this
            int input_index = stoi(input.substr(input.find_first_of("0123456789")));
            log_assert(
                input_index < base_op_info.input_names.size(),
                "input={} specified is outside the number of inputs provided for this fusion op={}",
                input,
                base_op_info.name);
            log_assert(
                input_index < base_op_info.input_data_formats.size(),
                "input={} specified is outside the number of input data formats provided for this fusion "
                "op={}",
                input,
                base_op_info.name);
            new_op_info.input_names.push_back(input);
            new_op_info.input_data_formats.push_back(base_op_info.input_data_formats.at(input_index));
        } else if (input.find("intermed") == 0) {
            // Using intermediate buffer as input.  Make sure the right input dataformat is picked
            int intermed_index = stoi(input.substr(input.find_first_of("0123456789")));
            log_assert(
                intermed_index < fused_op_info.num_intermediates,
                "input={} specified is outside the number of intermediates expected for this "
                "fused_op_id={}",
                input,
                fused_op_info.name);
            new_op_info.input_names.push_back(input);
            new_op_info.input_data_formats.push_back(base_op_info.intermed_data_format);
        } else if (input.find("dest") == 0) {
            // Using intermediate buffer as input.  Make sure the right input dataformat is picked
            new_op_info.input_names.push_back(input);
            new_op_info.input_data_formats.push_back(base_op_info.intermed_data_format);
        } else {
            log_fatal(
                "inputs in fused_op={} in fused_op_id={} must start with input or intermed -- inputN or "
                "intermedN", scheduled_op.name, fused_op_info.name);
        }
    }
    // Save input tms
    new_op_info.input_tm_ops = scheduled_op.input_tm_ops;
    // Save the matmul attributes if they are set in the scheduled_op
    if (scheduled_op.m_k > 0) {
        new_op_info.attributes.m_k = scheduled_op.m_k;
    }
    if (scheduled_op.u_kt > 0) {
        new_op_info.attributes.u_kt = scheduled_op.u_kt;
    }
    if (scheduled_op.vector_mode != Dim::Invalid) {
        new_op_info.attributes.vector_mode = scheduled_op.vector_mode;
    }
    if (scheduled_op.reduce_type != ReduceFunc::Invalid) {
        new_op_info.attributes.reduce_type = scheduled_op.reduce_type;
    }
    if (scheduled_op.reduce_dim != Dim::Invalid) {
        new_op_info.attributes.reduce_dim = scheduled_op.reduce_dim;
    }
    new_op_info.attributes.relu_en = scheduled_op.relu_en;
    new_op_info.attributes.relu_threshold = scheduled_op.relu_threshold;
    new_op_info.attributes.relu_mode = scheduled_op.relu_mode;
    new_op_info.attributes.probability = scheduled_op.probability;
    new_op_info.attributes.scale = scheduled_op.scale;
    new_op_info.attributes.seed = scheduled_op.seed;
    new_op_info.attributes.slope = scheduled_op.slope;
    new_op_info.attributes.exponent = scheduled_op.exponent;
    new_op_info.attributes.zero_point = scheduled_op.zero_point;

    // Save output dims -- Not needed for golden.
    new_op_info.output_dim = scheduled_op.output_dim;
}
std::vector<tt_op_info> netlist_utils::get_list_of_tt_op_info_from_fused_op(
    const tt_op_info& base_op_info, const unordered_map<string, tt_fused_op_info>& fused_ops_map) {
    if (base_op_info.type == "fused_op") {
        const auto& fused_op_info = base_op_info.fused_op_info;

        // Go through schedules one by one and flatten into a 1-D list of tt_op_infos
        std::unordered_map<std::string, std::string> output_name_to_op_name = {};
        std::vector<tt_op_info> result_op_infos;

        for (const auto& schedule : fused_op_info.schedules) {
            for (const auto& scheduled_op : schedule.scheduled_ops) {
                result_op_infos.push_back(base_op_info);
                update_fields_for_op_info_from_scheduled_op_info(
                    base_op_info, scheduled_op, fused_op_info, result_op_infos.back());
            }
        }
        return result_op_infos;
    } else {
        return {base_op_info};
    }
}

/**
 * Parses tt_fused_op_info structure to capture input dimensions
 * of the ops, infer buffer forking and other things used for
 * netlist checks and implementing certain logic.
 * 
 * NOTE: Function does not infer dimensions of external inputs.
*/
std::unordered_map<std::string, tt_fused_subop_info> netlist_utils::parse_fused_op_info(const tt_fused_op_info& t) {
    // The output structure
    std::unordered_map<std::string, tt_fused_subop_info> fused_subops_info;

    // Parser state machine states
    std::unordered_map<std::string, tt_dim_info> input_dim_info_state;
    std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> buffer_references_state;
    std::unordered_map<std::string, std::string> producer_id_state;

    // Iterate over fused op schedules
    for (const auto& schedule_info : t.schedules)
    {
        // Iterate over subops inside the schedule
        for (const auto& scheduled_op_info : schedule_info.scheduled_ops)
        {
            tt_fused_subop_info subop_info;

            log_assert(
                fused_subops_info.find(scheduled_op_info.name) == fused_subops_info.end(),
                "Duplicate op_name={} inside fused op "
                "fused_op_id={}",
                scheduled_op_info.name,
                t.name);

            subop_info.id = scheduled_op_info.name;
            subop_info.output_dim = scheduled_op_info.output_dim;
            
            // Iterate over inputs of the subop
            for (const auto& buffer_name : scheduled_op_info.input_names)
            {
                tt_fused_subop_input_info input_info;
                if (producer_id_state.find(buffer_name) != producer_id_state.end())
                {
                    input_info.id = producer_id_state[buffer_name];
                }

                // Check whether any subop currently uses the same buffer as input
                if (buffer_references_state.find(buffer_name) == buffer_references_state.end())
                {
                    input_info.is_forked = false;
                }
                else
                {
                    input_info.is_forked = true;

                    // Update fork info for all previous subops currently using the same buffer as input
                    for (const auto& [subop_id, input_index] : buffer_references_state[buffer_name])
                    {
                        fused_subops_info[subop_id].inputs[input_index].is_forked = true;
                    }
                }

                input_info.input_dim = input_dim_info_state[buffer_name];

                // Parse tms
                if (scheduled_op_info.input_tm_ops.find(subop_info.inputs.size()) != scheduled_op_info.input_tm_ops.end())
                {
                    for (auto tm: scheduled_op_info.input_tm_ops.at(subop_info.inputs.size())) {
                        if (netlist_utils::get_tm_op(tm.first) == TmOp::rBroadcast) {
                            input_info.r_bcast_factor = tm.second.at(0);
                        }
                        else if (netlist_utils::get_tm_op(tm.first) == TmOp::cBroadcast) {
                            input_info.c_bcast_factor = tm.second.at(0);
                        }
                    }
                }

                subop_info.inputs.push_back(input_info);
            }

            // Add input buffers to the references list
            // NOTE: This needs to be done after the previous loop to handle the case
            // where binary op has the same buffer on both inputs.
            int i = 0;
            for (const auto& buffer_name : scheduled_op_info.input_names)
            {
                buffer_references_state[buffer_name].push_back({subop_info.id, i});
                i++;
            }

            input_dim_info_state[scheduled_op_info.output] = scheduled_op_info.output_dim;
            producer_id_state[scheduled_op_info.output] = subop_info.id;
            // Clear the references list for the buffer used as output
            buffer_references_state.erase(scheduled_op_info.output);

            fused_subops_info[subop_info.id] = subop_info;
        }
    }

    return fused_subops_info;
}
