// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/base.hpp"
#include "common/size_lib.hpp"
#include "device/tt_xy_pair.h"
namespace router {
  class Router;
};
class Net2Pipe;

namespace tt {

enum class RouterBufferType : int {
    // An "unpacker" buffer. These buffers hold the inputs/operand contents of an op
    Input = 0,

    // A "packer" buffer. These buffers hold the results/output of an op
    Output = 1,

    // These buffers are used for scratch-space buffering during op execution. Typically these overlap with output
    // buffers but not always, especially for fused ops
    Intermediate = 2,

    // Corresponds to a buffer (queue) in DRAM. These buffers do not live in L1.
    DRAM = 3,

    // Any generic buffer that has a single input and single output pipe. This is the catch-all for all buffers that
    // don't fit into the other categories.
    Relay = 4,

    PrologInter = 5,
};

class buffer_info {
  protected:
    friend router::Router;
    friend Net2Pipe;
    int _t;
    RouterBufferType _buffer_type; // FIXME: move this to the router buffer info class because it can also be used for DRAM buffers (queues)
    int _stream_id;
    int _ublock_rt;
    int _ublock_ct;
    int _total_epoch_tiles; // # tiles that are expected to pass through the buffer this epoch
    int _mblock_m;
    int _mblock_n;
    int _mblock_k;
    int _allocated_size_tiles;
    int _scatter_gather_num_tiles;
    int _replication_factor;
    DataFormat _data_format;
    tt_xy_pair _tile_dims;
    bool _untilized;
    bool _is_scatter;

  public:
    bool operator==(buffer_info const&) const;
    buffer_info(const buffer_info &) = default;
    buffer_info(RouterBufferType buffer_type, int stream_id, const buffer_info &info_to_copy);
    buffer_info(
        RouterBufferType buffer_type,
        int stream_id,
        int t,
        int allocated_size_tiles,
        int scatter_gather_num_tiles,
        int total_epoch_tiles,
        int replication_factor,
        DataFormat data_format,
        tt_xy_pair const& tile_dims,
        bool untilized = false,
        bool is_scatter = false);
    buffer_info(
        RouterBufferType buffer_type,
        int stream_id,
        int t,
        int allocated_size_tiles,
        int ublock_rt,
        int ublock_ct,
        int input_count,
        int mblock_m,
        int mblock_n,
        int scatter_gather_num_tiles,
        int num_epoch_tiles,
        int replication_factor,
        DataFormat data_format,
        tt_xy_pair const& tile_dims,
        bool untilized = false,
        bool is_scatter = false);

    /*
     * Returns the operand ID this buffer is mapped to
     */
    int stream_id() const { return _stream_id; }

    /*
     * Returns the t dimension of the buffer shape (not allocation related)
     */
    int t() const { return _t; }

    /*
     * Returns the ublock-rt dimension of the buffer shape (not allocation related)
     */
    int ublock_rt() const { return _ublock_rt; }

    /*
     * Returns the ublock-ct dimension of the buffer shape (not allocation related)
     */
    int ublock_ct() const { return _ublock_ct; }

    /*
     * Returns the number of tiles in L1 allocated for storage required for this buffer
     */
    int allocated_size_in_tiles() const { return _allocated_size_tiles; }

    /*
     * Returns the number of bytes in L1 allocated for storage required for this buffer
     */
    int allocated_size_in_bytes() const;

    /*
     * Returns the scatter-gather number of tiles for the buffer. This field is used to define switching granularity for
     * consunmers. For example, if this buffer is a scatter buffer feeding a pipe, the value will dictate to the pipe
     * how many tiles to forward before advancing to the next buffer in the input buffer list.
     *
     * For unpacker buffers, this value defines the number of tiles moved per math iteration.
     *
     * See PipeGen specification for more details.
     */
    int scatter_gather_num_tiles() const { return _scatter_gather_num_tiles; }

    /*
     * Returns the total number of tiles that are expected to "pass through" this buffer this epoch (not allocation
     * related)
     */
    int total_epoch_tiles() const { return _total_epoch_tiles; }
    
    /*
     * Returns the mblock-m dimension of the buffer shape (not allocation related)
     */
    int mblock_m() const { return _mblock_m; }

    /*
     * Returns the mblock-n dimension of the buffer shape (not allocation related)
     */
    int mblock_n() const { return _mblock_n; }

    /*
     * Returns the mblock-k dimension of the buffer shape (not allocation related)
     */
    int mblock_k() const { return _mblock_k; }

    /*
     * Returns the dataformat of tile data in this buffer
     */
    DataFormat data_format() const { return _data_format; }

    /*
     * If the buffer is untilized, it will not hold a tile header and will be treated as raw data.
     * Otherwise, it will hold a tile header for each tile. Tilized buffers are routeable in overlay.
     */
    bool is_untilized() const { return _untilized; }

    /*
     * Returns true if this buffer is a scatter chunk of a larger parent buffer or if it's a parent buffer
     * that has scatter chunks. A scatter chunk is just a subset of sequential tiles in the parent buffer.
     */
    bool is_scatter() const { return _is_scatter; }

    /*
     * Returns true if this buffer has been assigned to an operand ID yet
     */
    bool is_assigned_to_operand_id() const { return _stream_id != -1; }

    /*
     * Returns the logical size of the buffer in tiles (not the number of allocated tiles)
     * Would typically correspond to mblock # tiles
     */
    int size_in_tiles() const;

    /*
     * Returns the logical size of the buffer in tiles (not the number of allocated tiles)
     * Would typically correspond to mblock # bytes
     */
    int size_in_bytes() const;

    /*
     * Returns the buffer type
     */
    RouterBufferType type() const { return this->_buffer_type; }

    /*
     * Returns the replication factor of the buffer. >= 1 for scatter buffers where the value represents
     * the number of equivalent buffers to instantiate. In this case, an equivalent buffer is one with the same
     * size and scatter_gather_num_tiles. Each replicated buffer will increment in the parent buffer by this
     * buffer's scatter_gather_num_tiles.
     *
     * e.g. if a buffer has scatter_gather_num_tiles = 4 and replication_factor = 8, and scatter ID = 0x20,
     * it would indicate to consumer components (e.g. pipegen yaml) that there exists 8 chunks in the buffer list:
     * 0x20 (this one), 0x24(replica 1), 0x28(replica 2), 0x2c, 0x30, 0x34, 0x38, 0x3c(replica 7)
     */
    int replication_factor() const { return this->_replication_factor; }

    /*
     * Number of bytes required to store a single tile of this buffer.
     */
    int tile_size_in_bytes() const { return tt::size::get_tile_size_in_bytes(_data_format, !_untilized, _tile_dims.y, _tile_dims.x); }

    /* Tile dimensions for the tiles held by this buffer
     */
    tt_xy_pair const& tile_dims() const { return _tile_dims; }
};

}; // namespace tt