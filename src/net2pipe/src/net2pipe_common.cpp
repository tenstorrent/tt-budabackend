// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "net2pipe_logger.h"
#include "net2pipe_common.h"
#include "size_lib.hpp"
#include "tile_maps_common.h"

int env_var(const char *env_var_name, int default_val) {
    const char *env_var_val = std::getenv(env_var_name);
    if (env_var_val == nullptr) {
        return default_val;
    }
    return std::stoi(env_var_val);
}

namespace n2p {

bool op_has_intermediate_buf(const tt_op_info &op_info) {
    if (get_op_class(op_info) == OpClass::MatMul) {
        return (op_info.attributes.identity) || (op_info.attributes.bias) || (op_info.gradient_op) ||
               (op_info.attributes.requant) || (op_info.attributes.dequant) || (op_info.attributes.m_k > 1) ||
               op_info.attributes.accumulate;
    }
    if (get_op_class(op_info) == OpClass::Depthwise) {
        return (op_info.attributes.m_k > 1) || (op_info.attributes.bias) || (op_info.gradient_op);
    }
    if (op_is_topk(op_info)) {
        return true;
    }
    return ((get_op_class(op_info) == OpClass::Reduce) && (op_info.attributes.reduce_dim == Dim::Z) &&
            (op_info.attributes.z > 1)) ||
           ((get_op_class(op_info) == OpClass::FusedOp) && (op_info.fused_op_info.num_intermediates > 0)) ||
           op_info.gradient_op;
}

int op_num_intermediate_buf(const tt_op_info &op_info) {
    if (get_op_class(op_info) == OpClass::FusedOp) {
        return op_info.fused_op_info.num_intermediates;
    } else if (op_is_topk(op_info)) {
        return 2;
    } else {
        return 1;
    }
}

bool op_has_output_buf(const tt_op_info &op_info) { return !op_info.gradient_op; }

bool op_is_fused(const tt_op_info &op_info) { return (get_op_class(op_info) == OpClass::FusedOp); }

bool op_is_topk(const tt_op_info &op_info) { return (get_op_class(op_info) == OpClass::Topk); }

bool op_output_buffer_shared_with_intermediate(const tt_op_info &op_info) {
    return (op_has_output_buf(op_info) && op_has_intermediate_buf(op_info) && !op_info.untilize_output &&
            !op_is_fused(op_info) && !op_is_topk(op_info) && (op_info.intermed_data_format == op_info.output_data_format)) ||
           netlist_utils::is_valid_ethernet_op(op_info.type);
}

OpClass get_op_class(const tt_op_info &op_info) {
    if (op_info.type == "matmul") {
        return OpClass::MatMul;
    } else if (op_info.type == "reduce") {
        return OpClass::Reduce;
    } else if (op_info.type == "fused_op") {
        return OpClass::FusedOp;
    } else if (op_info.type == "splice") {
        return OpClass::Nary;
    } else if (op_info.type == "buffer") {
        return OpClass::Buffer;
    } else if (op_info.type == "embedding") {
        return OpClass::Embedding;
    } else if (op_info.type == "tilizer") {
        return OpClass::Tilizer;
    } else if (op_info.type == "depthwise") {
        return OpClass::Depthwise;
    } else if (op_info.type == "topk") {
        return OpClass::Topk;
    } else if (op_info.input_names.size() == 2) {
        return OpClass::EltwiseBinary;
    } else if (op_info.input_names.size() == 1) {
        return OpClass::EltwiseUnary;
    } else {
        ERROR(
            "Op: " + op_info.name << ", unknown type: " + op_info.type);
    }
}

int get_op_output_mblock_tiles(const tt_op_info &op_info) {
  return  get_mblock_size_tiles(op_info);
}

int get_op_num_input_bufs(const tt_op_info &op_info) {
    switch (n2p::get_op_class(op_info)) {
        case OpClass::MatMul: if ((op_info.attributes.identity && op_info.attributes.bias)||
                                  ((op_info.attributes.requant || op_info.attributes.dequant) && op_info.attributes.bias)) return 4;
                              else if (op_info.attributes.identity || op_info.attributes.bias || op_info.attributes.requant || op_info.attributes.dequant) return 3;
                              else return 2;
        case OpClass::EltwiseBinary: return 2;
        case OpClass::FusedOp: return op_info.input_names.size();
        case OpClass::Nary: return op_info.input_names.size();
        case OpClass::Buffer: return op_info.input_names.size();
        case OpClass::Embedding: return op_info.input_names.size();
        case OpClass::Tilizer: return op_info.input_names.size();
        case OpClass::Depthwise: return op_info.input_names.size();
        case OpClass::Topk: return op_info.input_names.size();
        default: return 1;
    }
}

// Single buffer size, which is half buf_size_mb or 1
int get_op_output_single_buf_size_mblocks(const tt_op_info &op_info) {
  if (op_info.buf_size_mb == 1) {
    return op_info.buf_size_mb;
  } else {
    if (op_info.buf_size_mb % 2) {
      ERROR("Op name " + op_info.name + " has odd buf_size_mb = " + std::to_string(op_info.buf_size_mb) +
            ". Presently buf_size_mb > 1 is interpreted as double-buffering one or more t-slices, so it must be even.");
    }
    return op_info.buf_size_mb/2;
  }
} 


// Double buffer size, which is full buf_size_mb
int get_op_output_full_buf_size_mblocks(const tt_op_info &op_info) {
  return op_info.buf_size_mb;
} 


int get_op_output_tiles_per_input(const tt_op_info& op_info) {
  return get_mblock_size_tiles(op_info) * op_info.output_dim.t;
}

int get_tile_xy_size(TileDim tile_dim) {
    switch (tile_dim) {
        case TileDim::Dim32x16:
        case TileDim::Dim16x32: return 16 * 32; 
        case TileDim::Dim8x32:  return 8 * 32; 
        case TileDim::Dim4x32:  return 4 * 32; 
        case TileDim::Dim2x32:  return 2 * 32; 
        case TileDim::Dim1x32:  return 1 * 32; 
        case TileDim::Dim32x32:  return 32 * 32;
        default: ERROR("Unsupported tile dim: " << std::to_string((std::uint32_t)tile_dim)); 
    }    
}

int get_tile_exp_sec_size(TileDim tile_dim) {
    switch (tile_dim) {
        case TileDim::Dim1x32:
        case TileDim::Dim2x32:
        case TileDim::Dim4x32:
        case TileDim::Dim8x32:  return 16;
        case TileDim::Dim32x16:
        case TileDim::Dim16x32: return 32;
        case TileDim::Dim32x32: return 64;
        default: ERROR("Unsupported tile dim: " << std::to_string((std::uint32_t)tile_dim)); 
    }    
}

int get_tile_height(TileDim tile_dim) {
    switch (tile_dim) {
        case TileDim::Dim32x32:
        case TileDim::Dim32x16: return 32;
        case TileDim::Dim16x32: return 16;
        case TileDim::Dim8x32: return 8;
        case TileDim::Dim1x32: return 1;
        case TileDim::Dim2x32: return 2;
        case TileDim::Dim4x32: return 4;

        case TileDim::Invalid:
        default: log_assert(false, "Invalid tile dim to get height of"); return -1;  // for warnings
    };
}

int get_tile_width(TileDim tile_dim) {
    switch (tile_dim) {
        case TileDim::Dim32x32:
        case TileDim::Dim16x32:
        case TileDim::Dim1x32:
        case TileDim::Dim2x32:
        case TileDim::Dim4x32:
        case TileDim::Dim8x32: return 32;

        case TileDim::Dim32x16: return 16;
        

        case TileDim::Invalid:
        default: log_assert(false, "Invalid tile dim to get width of");
            return -1; // for warnings
    };
}

int get_format_tile_size_bytes(tt::DataFormat format, bool has_header, TileDim tile_dim) {
    return tt::size::get_tile_size_in_bytes(format, has_header, get_tile_height(tile_dim), get_tile_width(tile_dim));
}

bool is_bfp_format(DataFormat df) {
    return (df == DataFormat::Bfp8 || df == DataFormat::Bfp8_b) ||
           (df == DataFormat::Bfp4 || df == DataFormat::Bfp4_b) || (df == DataFormat::Bfp2 || df == DataFormat::Bfp2_b);
}

}; // namespace n2p
