// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace pipegen2 {
namespace constants {

// Logical location that is not mapped to a physical location.
constexpr int unmapped_logical_location = 255;

// Default tile size.
constexpr unsigned int default_tile_size = 32;

// Size of tile header buffer, which dictates the maximum number of tiles that can be transferred in one phase in
// theory on given architecture.
constexpr unsigned int general_max_num_tiles_per_phase = 2048;

// ------------------------------------ L1 buffer management constants -------------------------------------------------
// Tile header size in bytes.
constexpr unsigned int tile_header_size_bytes = 16;

// Tile header buffers have fixed size.
constexpr unsigned int tile_header_buffer_size_bytes = tile_header_size_bytes * general_max_num_tiles_per_phase;

// Unused space at the end of L1 data buffers space, because ckernels can wrap to 0 if we try to use entire L1.
constexpr int unused_data_buffers_space_bytes = 64;
// ---------------------------------------------------------------------------------------------------------------------

// ------------------------------------ DRAM IO constants --------------------------------------------------------------
// Maximum number of tiles we can transfer across noc from DRAM in one request.
constexpr unsigned int dram_input_stream_max_pending_read_tiles = 64 * 1024 - 1;

// Maximum number of bytes we can transfer across noc from DRAM in one request.
constexpr unsigned int dram_input_stream_max_pending_read_bytes = 52 * 1024;

// Maximum number of tiles we can transfer across noc to DRAM in one request.
constexpr unsigned int dram_output_stream_max_write_issue_tiles = 64 * 1024 - 1;

// Maximum number of bytes we can transfer across noc to DRAM in one request.
constexpr unsigned int dram_output_stream_max_write_issue_bytes = 80 * 1024;

// Minimal size of dram receiver stream in bytes.
constexpr unsigned int dram_rec_stream_min_size_bytes = 52 * 1024;
constexpr unsigned int eth_dram_rec_stream_min_size_bytes = 32 * 1024;

// Maximal size of dram receiver stream in bytes.
constexpr unsigned int pipe_reduce_mem_usage_threshold_bytes = 100 * 1024;
constexpr unsigned int eth_pipe_reduce_mem_usage_threshold_bytes = 32 * 1024;
// ---------------------------------------------------------------------------------------------------------------------

constexpr unsigned int mmio_dram_write_outgoing_noc_vc = 0;

}  // namespace constants
}  // namespace pipegen2
