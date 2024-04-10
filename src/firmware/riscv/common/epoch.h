// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _EPOCH_H_
#define _EPOCH_H_

#include <cstdint>
#include <cstddef>

#include "l1_address_map.h"
#include "eth_l1_address_map.h"
#include "noc_overlay_parameters.h"
#include "stream_io_map.h"
#include "epoch_q.h"
#include "risc_attribs.h"

const uint32_t EPOCH_INFO_ADDR = l1_mem::address_map::OVERLAY_BLOB_BASE;
//const uint32_t BLOB_START_ADDR = (l1_mem::address_map::OVERLAY_BLOB_BASE + 0x1000);

const uint32_t ETH_EPOCH_INFO_ADDR = eth_l1_mem::address_map::OVERLAY_BLOB_BASE;


#define EPOCH_INFO_PTR ((volatile epoch_t tt_l1_ptr *)EPOCH_INFO_ADDR)
#define ETH_EPOCH_INFO_PTR ((volatile epoch_t tt_l1_ptr *)ETH_EPOCH_INFO_ADDR)

const uint32_t EPOCH_Q_BASE = l1_mem::address_map::NCRISC_L1_EPOCH_Q_BASE;
const uint32_t ETH_EPOCH_Q_BASE = eth_l1_mem::address_map::L1_EPOCH_Q_BASE;
#define L1_EPOCH_Q_PTR  ((volatile epoch_queue::EpochQHeader_t tt_l1_ptr *)EPOCH_Q_BASE)
#define ETH_L1_EPOCH_Q_PTR  ((volatile epoch_queue::EpochQHeader_t tt_l1_ptr *)ETH_EPOCH_Q_BASE)

// Kernel parameters are subsumed under inputs.
// Kernel intermediates are inputs/outputs that map to the same stream.
const uint32_t EPOCH_MAX_INPUTS = 24;
const uint32_t EPOCH_MAX_OUTPUTS = 16; // Maximum number of outputs supported by Ncrisc cores.
const uint32_t EPOCH_MAX_OUTPUTS_ETH = 24; // Special define for ethernet cores to support upto 24 firmware managed ethernet streams.
const uint32_t EPOCH_MAX_OUTPUT_FORKS = 16;
const uint32_t EPOCH_MAX_NUM_TILE_SIZES = 8;

enum datacopy_stream_type_t: uint8_t {
  NONE = 0,
  UNPACKER,
  PACKER
};

enum stream_state_t {
  STREAM_STATE_START = 0,
  STREAM_STATE_ACTIVE,
  STREAM_STATE_EPOCH_DONE
};

#define STREAM_INPUT_PARK     (((uint32_t)0x1) << 0)
#define STREAM_OUTPUT_PARK    (((uint32_t)0x1) << 7)
#define STREAM_DRAM_IO        (((uint32_t)0x1) << 1)
#define STREAM_DRAM_STREAMING (((uint32_t)0x1) << 5)
#define STREAM_DRAM_INPUT     (((uint32_t)0x1) << 2)
#define STREAM_DRAM_OUTPUT    (((uint32_t)0x1) << 3)
#define STREAM_INTERMEDIATE   (((uint32_t)0x1) << 4)
#define STREAM_MOVES_RAW_DATA (((uint32_t)0x1) << 6)
#define STREAM_IS_FORK        (((uint32_t)0x1) << 8)
#define STREAM_DRAM_RAM       (((uint32_t)0x1) << 9)
#define STREAM_BRISC_PACK     (((uint32_t)0x1) << 10)
#define STREAM_DRAM_NO_PUSH   (((uint32_t)0x1) << 11)
#define STREAM_NCRISC_CLEAR   (((uint32_t)0x1) << 12)

#pragma pack(push)
#pragma pack(4)

struct alignas(NOC_ADDRESS_ALIGNMENT) DramPollingCtrl_t {
    uint32_t poll_req;
    uint32_t poll_req_ack;
    uint32_t poll_freq;
};

static_assert((sizeof(DramPollingCtrl_t)== NOC_ADDRESS_ALIGNMENT), "DramPollingCtrl_t size misaligned");

const uint32_t DRAM_POLLING_CTRL_BASE = l1_mem::address_map::NCRISC_L1_DRAM_POLLING_CTRL_BASE;
const uint32_t ETH_DRAM_POLLING_CTRL_BASE = eth_l1_mem::address_map::L1_DRAM_POLLING_CTRL_BASE;
#define DRAM_POLLING_CTRL_PTR  ((volatile DramPollingCtrl_t tt_l1_ptr *)DRAM_POLLING_CTRL_BASE)
#define ETH_DRAM_POLLING_CTRL_PTR  ((volatile DramPollingCtrl_t tt_l1_ptr *)ETH_DRAM_POLLING_CTRL_BASE)

struct EpochRuntimeConfig_t {
  uint32_t overlay_size_bytes;
  uint32_t dram_header_polling_freq;
};
const uint32_t EPOCH_RUNTIME_CONFIG_B = l1_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE;
const uint32_t ETH_EPOCH_RUNTIME_CONFIG_B = eth_l1_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE;
#define EPOCH_RUNTIME_CONFIG_PTR  ((volatile EpochRuntimeConfig_t tt_l1_ptr *)EPOCH_RUNTIME_CONFIG_B)
#define ETH_EPOCH_RUNTIME_CONFIG_PTR  ((volatile EpochRuntimeConfig_t tt_l1_ptr *)ETH_EPOCH_RUNTIME_CONFIG_B)

#define DRAM_IO_STATE_RD_SEC 0
#define DRAM_IO_STATE_WR_SEC 8

// Pointers on our RISCV C++ compiler are 4 bytes. By default, our backend compiler has 8 byte pointers.
// When compiling for backend compiler, use uint32_t which would point to address on the blob itself.
#ifdef TENSIX_FIRMWARE
// Forward declaration is needed.
struct scatter_pipe_blob_t;
struct scatter_pipe_state_t;
struct dram_io_scatter_state_t;
struct dram_io_state_t;
struct epoch_stream_dram_io_info_t;
struct epoch_stream_info_t;
using scatter_pipe_blob_t_ptr = scatter_pipe_blob_t tt_l1_ptr *;
using scatter_pipe_state_t_ptr = scatter_pipe_state_t tt_l1_ptr *;
using scatter_offsets_ptr = volatile tt_uint64_t tt_l1_ptr *;
using dram_io_scatter_state_t_ptr = dram_io_scatter_state_t tt_l1_ptr *;
using dram_io_state_ptr = dram_io_state_t tt_l1_ptr *;
using dram_io_state_ptr_volatile = volatile dram_io_state_t tt_l1_ptr *;
using epoch_stream_dram_io_info_t_ptr = epoch_stream_dram_io_info_t tt_l1_ptr *;
using epoch_stream_info_t_ptr = epoch_stream_info_t tt_l1_ptr *;
#else
using scatter_pipe_blob_t_ptr = uint32_t;
using scatter_pipe_state_t_ptr = uint32_t;
using scatter_offsets_ptr = uint32_t;
using dram_io_scatter_state_t_ptr = uint32_t;
using dram_io_state_ptr = uint32_t;
using dram_io_state_ptr_volatile = uint32_t;
using epoch_stream_dram_io_info_t_ptr = uint32_t;
using epoch_stream_info_t_ptr = uint32_t;
#endif

struct scatter_pipe_blob_t {
    uint32_t scatter_blob_base_addr;
    uint32_t start_scatter_blob_num_cfg_regs;
};

struct scatter_pipe_state_t {
  uint32_t curr_unroll;
  uint32_t max_unroll;
  uint32_t unused0;
  scatter_pipe_blob_t_ptr unroll_blobs;
};

struct alignas(NOC_ADDRESS_ALIGNMENT) dram_io_scatter_state_t {
  uint32_t unused1;
  uint32_t scatter_offsets_size;
  uint32_t scatter_chunk_size_bytes;
  uint32_t q_slot_size_bytes;
  uint32_t scatter_chunk_size_tiles;
  uint32_t unused2;
  uint32_t unused3;
  scatter_offsets_ptr scatter_offsets;
};

struct alignas(NOC_ADDRESS_ALIGNMENT) dram_io_state_t {
  // Section for dram to l1 traffic
  struct alignas(NOC_ADDRESS_ALIGNMENT) dram_to_l1_t
  {
    uint16_t rd_dram_rdptr;           // Word 0
    uint16_t rd_grd_ptr_autoinc;
    uint16_t rd_dram_wrptr;           // Word 1
    uint16_t rd_gwr_ptr_autoinc;
    uint16_t rd_dram_local_rdptr;     // Word 2
    uint16_t rd_epoch_id_tag;
    uint16_t rd_stride;               // Word 3
    uint16_t rd_flags;
    uint16_t rd_lrd_ptr_autoinc;      // Word 4
    uint16_t unused0;
    uint16_t rd_queue_update_stride;  // Word 5
    uint16_t unused1;
    uint32_t dram_streaming_epoch_id; // Word 6
    uint32_t dram_streaming_tag;      // Word 7
    // if NOC_ADDRESS_ALIGNMENT==32, the size of this structure is 32 bytes, and all fields have been utilized.
    // if NOC_ADDRESS_ALIGNMENT==64, the size of this structure is 64 bytes, and there are unused 32bytes.
  } dram_to_l1;

  // Section for l1 to dram traffic
  struct alignas(NOC_ADDRESS_ALIGNMENT) l1_to_dram_t
  {
    uint16_t wr_dram_rdptr;           // Word 0
    uint16_t wr_grd_ptr_autoinc;
    uint16_t wr_dram_wrptr;           // Word 1
    uint16_t wr_gwr_ptr_autoinc;
    uint16_t wr_dram_local_rdptr;     // Word 2
    uint16_t wr_epoch_id_tag;
    uint16_t wr_stride;               // Word 3
    uint16_t wr_flags;

    union {                           // Word 4,5
      struct {
        uint16_t wr_lrd_ptr_autoinc;
        uint16_t unused2;
        uint16_t wr_queue_update_stride;
        uint16_t unused3;
      };
      struct {
        tt_uint64_t dram_streaming_header_addr;
      };
    };
    uint32_t data_chunk_size_bytes;   // Word 6
    uint16_t data_chunk_size_tiles;   // Word 7
    uint8_t dram_padding;
    // if NOC_ADDRESS_ALIGNMENT==32, the size of this structure is 32 bytes, and there is 1 unused byte at the end.
    // if NOC_ADDRESS_ALIGNMENT==64, the size of this structure is 64 bytes, and there are 33 unused bytes at the end.
    } l1_to_dram;

  // Temp variables
  struct alignas(NOC_ADDRESS_ALIGNMENT) local_t
  {
    uint32_t dram_buf_size_bytes;
    uint32_t dram_buf_size_q_slots; // unused
    tt_uint64_t dram_buf_addr;
    uint32_t dram_q_slot_size_tiles;
    uint8_t reader_index;
    uint8_t total_readers;
    uint8_t dram_decoupled; // used as flag to signal that ncrisc shoud skip reading/writing to this dram buffer
                            // as dram has been decoupled for this op.
                            // This flag is set by ncrisc in risc_dram_stream_handler_init_l1( )
    uint8_t stride_wrap;
    dram_io_scatter_state_t_ptr dram_io_scatter_state;
    dram_io_state_ptr next;
    // if NOC_ADDRESS_ALIGNMENT==32, the size of this structure is 32 bytes, and all fields have been utilized.
    // if NOC_ADDRESS_ALIGNMENT==64, the size of this structure is 64 bytes, and there are unused 32bytes.
  } local;
};

struct alignas(NOC_ADDRESS_ALIGNMENT) epoch_stream_dram_io_info_t {
  uint32_t dram_q_slot_size_bytes;
  uint8_t input_noc;
  uint8_t output_noc;
  uint8_t dram_output_no_push;
  uint8_t has_multi_readers;
  uint16_t scatter_list_stream_id;
  union{
    uint16_t scatter_list_indicies_per_tile;
    uint16_t c_dim_loop_num_rows;
  };
  union{
    uint32_t scatter_list_indicies_per_input;
    uint32_t tilize_row_col_offset;
  };
  uint32_t dram_embeddings_row_shift;
  uint32_t epoch_q_slots_remaining;
  uint16_t dram_embeddings_row_tiles;
  uint8_t dram_writes_with_cmd_buf;
  uint8_t hw_tilize;
  dram_io_state_ptr_volatile dram_io_state;
};

struct alignas(NOC_ADDRESS_ALIGNMENT) epoch_stream_info_t {

  // Other stream state/info can be read from the stream registers directly,
  // no need to store it into this structure.
  uint16_t stream_id;
  uint16_t producer_epoch_id;
  uint16_t epoch_start_phase;
  datacopy_stream_type_t datacopy_stream_type;  // for ethernet datacopy -> for the other aliased stream, is it a "packer" or "unpacker"
  uint8_t  datacopy_stream_id;                  // for ethernet datacopy -> the "packer"/"unpacker" sharing buffer space with this stream
  uint32_t epoch_num_tiles;
  uint32_t tile_size_words;
  uint32_t buf_size_tiles;
  uint32_t buf_full_size_bytes;
  uint32_t buf_base_addr;
  uint16_t num_msgs_in_block;
  uint8_t start_phase_num_cfg_regs;
  uint8_t packer_operand;
  uint32_t msg_info_buf_start;
  uint32_t blob_start_addr;
  uint32_t blob_size;
  uint32_t num_iters_in_epoch;
  uint32_t epoch_iters_remaining;
  uint32_t num_scatter_inner_loop;
  uint8_t legacy_pack;
  uint8_t eth_remote_fw_stream_id;
  uint16_t num_mblock_buffering;

  uint32_t flags;
  uint16_t stride;
  uint16_t total_strides;
  uint32_t stride_offset_size_bytes;
  union{
    uint32_t skip_col_bytes;
    uint32_t pipe_scatter_output_loop_size;
  };
  union{
    uint32_t skip_col_tile_row_bytes;
    scatter_pipe_state_t_ptr scatter_pipe_state;
  };
  union{
    uint32_t skip_col_row_bytes;
    uint32_t scatter_idx;
  };
  union{
    uint32_t skip_zcol_bytes;
    uint32_t pipe_scatter_output_loop_count;
  };
  union{
    uint32_t skip_col_zrow_bytes;
    uint32_t num_iter_tiles;
  };
  uint16_t c_dim_size;
  uint16_t r_dim_size;
  uint16_t zc_dim_size;
  uint16_t zr_dim_size;

  // Section for epoch fw binaries, dont use for other things
  uint32_t unused0_fw;
  uint32_t unused1_fw;
  uint32_t unused2_fw;
  uint32_t unused3_fw;
  uint32_t unused4_fw;
  uint32_t unused5_fw;
  uint32_t unused6_fw;
  uint32_t unused7_fw;

  uint8_t untilize_copy_iters;
  uint8_t log_2x_untilize_copy_iters;
  uint16_t num_dram_io_bufs;
  epoch_stream_dram_io_info_t_ptr dram_io_info;
  uint16_t num_fork_streams;
  uint16_t scatter_order_size;
  uint8_t fork_idxs[EPOCH_MAX_OUTPUT_FORKS];
};

struct tile_clear_blob_t {
  uint32_t blob_words[16][6];
};

struct alignas(NOC_ADDRESS_ALIGNMENT) epoch_t {
  uint32_t num_inputs;
  uint32_t num_outputs;
  uint32_t num_active_streams;

  uint8_t  epoch_valid;
  uint8_t  all_streams_ready;
  uint8_t  all_streams_and_kernels_done;
  uint8_t  end_program;

  uint32_t num_tile_sizes;
  uint32_t tile_size_words[EPOCH_MAX_NUM_TILE_SIZES];
  uint32_t tile_size_header_buf_addr[EPOCH_MAX_NUM_TILE_SIZES];

  epoch_stream_info_t_ptr inputs[EPOCH_MAX_INPUTS];
  epoch_stream_info_t_ptr outputs[EPOCH_MAX_OUTPUTS_ETH];
  epoch_stream_info_t_ptr active_streams[NOC_NUM_STREAMS];

  uint32_t perf_dram_copy_req[l1_mem::address_map::PERF_NUM_THREADS];
  uint32_t perf_dram_copy_ack[l1_mem::address_map::PERF_NUM_THREADS];
  tt_uint64_t perf_dram_addr[l1_mem::address_map::PERF_NUM_THREADS];
  uint16_t perf_req_max[l1_mem::address_map::PERF_NUM_THREADS];
  uint16_t unused1[5];
  uint32_t extra_dram_q_state_addr;

  uint16_t ublock_rt;
  uint16_t ublock_ct;
  uint16_t mblock_m;
  uint16_t mblock_n;
  uint16_t mblock_k;
  uint8_t  has_eth_stream_trans;
  uint8_t  has_packer_mcast_opt;

  uint16_t overlay_valid; // signifies if core has valid overlay that it needs to load/run
  uint16_t skip_kernels; // if true, don't load/run trisc binaries - there are none for this epoch on this core.
  uint32_t epoch_id;
  tile_clear_blob_t tile_clear_blob;

  #if NOC_ADDRESS_ALIGNMENT > 32
  // Padding necessary to make size of this structure NOC_ADDRESS_ALIGNMENT bytes divisible. 
  // Needs to be added here since next two attributes *must* remain at the end of epoch_t. 
  uint8_t padding[NOC_ADDRESS_ALIGNMENT - 32];
  #endif

  uint32_t dummy_phase_tile_header_and_data[3]; // Needed to make dummy phases work, always set to 0x1, must always be at the end of epoch_t
  uint32_t overlay_blob_extra_size; // This is read part of dummy phase header but the bits should be ignored by streams, so they are repurposed for fw
};

// checking if the dummy_phase_tile_header_and_data is at the end of the epoch_t
static_assert(
    (offsetof(epoch_t, epoch_t::dummy_phase_tile_header_and_data) ==
     sizeof(epoch_t) - (sizeof(epoch_t::dummy_phase_tile_header_and_data) + sizeof(epoch_t::overlay_blob_extra_size))),
    "dummy_phase_tile_header_and_data wrong position");

static_assert(sizeof(epoch_stream_info_t_ptr) == 4);
static_assert(sizeof(epoch_stream_dram_io_info_t_ptr) == 4);
static_assert(sizeof(dram_io_state_ptr_volatile) == 4);
static_assert(sizeof(dram_io_state_ptr) == 4);
static_assert(sizeof(dram_io_scatter_state_t_ptr) == 4);
static_assert(sizeof(scatter_offsets_ptr) == 4);
static_assert(sizeof(scatter_pipe_state_t_ptr) == 4);
static_assert(sizeof(scatter_pipe_blob_t_ptr) == 4);

static_assert((alignof(epoch_t) == NOC_ADDRESS_ALIGNMENT), "epoch_t size misaligned");
static_assert((alignof(epoch_stream_info_t) == NOC_ADDRESS_ALIGNMENT), "epoch_stream_info_t size misaligned");
static_assert(
    (alignof(epoch_stream_dram_io_info_t) == NOC_ADDRESS_ALIGNMENT), "epoch_stream_dram_io_info_t size misaligned");
static_assert(
    (alignof(dram_io_state_t::dram_to_l1_t) == NOC_ADDRESS_ALIGNMENT), "dram_io_state_t::dram_to_l1_t size misaligned");
static_assert(
    (alignof(dram_io_state_t::l1_to_dram_t) == NOC_ADDRESS_ALIGNMENT), "dram_io_state_t::l1_to_dram_t size misaligned");
static_assert((alignof(dram_io_state_t::local_t) == NOC_ADDRESS_ALIGNMENT), "dram_io_state_t::local_t size misaligned");
static_assert((alignof(dram_io_state_t) == NOC_ADDRESS_ALIGNMENT), "dram_io_state_t size misaligned");
static_assert((alignof(dram_io_scatter_state_t) == NOC_ADDRESS_ALIGNMENT), "dram_io_scatter_state_t size misaligned");
// scatter_pipe_state_t is not aligned to NOC_ADDRESS_ALIGNMENT.
// scatter_pipe_blob_t is not aligned to NOC_ADDRESS_ALIGNMENT.

#pragma pack(pop)

#endif // ndef _EPOCH_H_
