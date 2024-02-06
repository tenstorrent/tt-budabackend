// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/model/base_types.hpp"
#include "common/size_lib.hpp"

namespace tt {

class tt_buffer;
class tt_tensor;

enum class TensorType : uint8_t
{
    Activation = 0,
    Parameter = 1,
    Constant = 2,
    Gradient = 3,
    OptimizerState = 4,
    OptimizerParameter = 5,
};

enum class BufferType : uint8_t
{
    Input = 0,
    Output = 1,
    Parameter = 2,
    Intermediate = 3,
    Relay = 4,
    Invalid = 5,
    TileBuffer = 6,
};

struct tt_tensor_metadata {
    tt_shape shape = {};
    DataFormat data_format = DataFormat::Invalid;
    DataFormat gradient_data_format = DataFormat::Invalid; // backpass gradient accumulation format
    DataFormat optimizer_data_format = DataFormat::Invalid;  // optimizer non-scalar parameter format
    TensorType tensor_type = TensorType::Activation;
    bool is_tilized = true;

    bool requires_grad = false;
    tt_tensor* gradient = nullptr;
    // Matmul transposes weights in backward_operand_zero and original weights need to be associated with the copy
    // Allows optimizer to update both
    tt_tensor* transposed_parameter_copy = nullptr;

    tt_op *producer_op_ptr = NULL;
    uint32_t producer_op_output_index = 0; // for multi-output ops

    tt_dram_io *dram_io = nullptr; // Currently only used for epilogue

    tt_block_shape block_shape = {0, 0};
    tt_grid_shape grid_shape = {0, 0};
};

struct tt_buffer_metadata {
    // --- BUFFER METADATA ----
    uint32_t id;
    BufferType type;

    // the size of the allocated L1 memory for the buffer, it's most commonly equal to 2 x volume(block)
    // don't equate/confuse with volume(buffer), although in some cases it may be possible to allocate in L1 for the entire buffer's volume
    int L1_allocated_size_num_tiles;
    int get_L1_allocated_size_bytes() const {
        return L1_allocated_size_num_tiles * size::get_tile_size_in_bytes(data_format, true);
    }
    int get_buffer_volume_num_bytes() const {
        return buffer_shape.volume() * size::get_tile_size_in_bytes(data_format, true);
    }

    // shape of the buffer, and tensor slice (position in the tensor)
    // TOOD: consider renaming tensor_slice to tensor_position
    tt_buffer_shape buffer_shape;
    tt_tensor_slice tensor_slice;

    DataFormat data_format = DataFormat::Invalid;
    int associated_dataflow_size;
    tt_core_coord core_coord;

    // --- BLOCK METADATA ----
    // the shape of the blocks within the buffer
    tt_block_shape streaming_block_shape;
    // the volume of the block (num tiles in the block)
    int num_tiles_in_streaming_block() const { return streaming_block_shape.volume(); }
    // the number of blocks in the buffer
    int num_streaming_blocks() const {
        if(buffer_shape.volume() % streaming_block_shape.volume() != 0) {
            log_fatal("buffer_shape not multiple of streaming block shape");
        }
        int block_cnt = buffer_shape.volume() / streaming_block_shape.volume();
        // cout << "block_cnt = " << block_cnt << endl;
        return block_cnt;
    }
};

struct tt_param_buffer_metadata : tt_buffer_metadata {
    tt_buffer* associated_dram_buffer;
};

inline ostream &operator<<(ostream &os, tt::BufferType const &buffer_type) {
    switch (buffer_type) {
        case tt::BufferType::Input: os << "BufferType::Input"; break;
        case tt::BufferType::Output: os << "BufferType::Output"; break;
        case tt::BufferType::Parameter: os << "BufferType::Parameter"; break;
        case tt::BufferType::Intermediate: os << "BufferType::Intermediate"; break;
        default: os << "BufferType::(UNKNOWN)"; break;
    }
    return os;
}

inline ostream &operator<<(ostream &os, tt::tt_buffer_metadata const &buffer_md) {
    os << "tt_buffer_metadata{";
    os << ".core_coord={" << buffer_md.core_coord.row << "," << buffer_md.core_coord.col << "}, ";
    os << ".id=" << buffer_md.id << ", ";
    os << ".type=" << buffer_md.type << ", ";
    os << ".data_format=DataFormat::" << buffer_md.data_format << ", ";
    os << ".L1_allocated_size_num_tiles=" << buffer_md.L1_allocated_size_num_tiles << ", ";
    os << ".buffer_shape=" << buffer_md.buffer_shape << ", ";
    os << ".streaming_block_shape=" << buffer_md.streaming_block_shape;
    os << "}";

    return os;
}

}  // namespace tt
