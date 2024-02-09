// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _STREAM_IO_MAP_
#define _STREAM_IO_MAP_

#include <stdint.h>
#include <stdbool.h>

/* 
   Kernel operand mapping scheme:
   - ID 0-7 (inputs, unpacker-only) => streams 8-15
   - ID 8-15 (params, unpacker-only) => streams 16-23
   - ID 16-23 (outputs, packer-only) => streams 24-31
   - ID 24-31 (intermediates, packer/unpacker) => streams 32-39
*/

const uint32_t MCAST_START_STREAM = 0;
const uint32_t MCAST_END_STREAM = 2;
const uint32_t OPERAND_START_STREAM = 8;
const uint32_t INPUT_START_STREAM = 8;
const uint32_t INPUT_PARAMS_START_STREAM = 16;
const uint32_t OUTPUT_START_STREAM = 24;
const uint32_t INTERMEDIATES_START_STREAM = 32;
const uint32_t END_IO_STREAM = 39;

const int OPERAND_INPUT_START_INDEX = 0;
const int OPERAND_INPUT_PARAMS_START_INDEX = 8;
const int OPERAND_OUTPUT_START_INDEX = 16;
const int OPERAND_INTERMEDIATES_START_INDEX = 24;
const int OPERAND_RELAY_START_INDEX = 32;
const int MAX_NUM_OPERANDS = 64;

#ifdef TENSIX_FIRMWARE
#include "risc_attribs.h"
#define MCAST_PACKER_OPT_EN  ((volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(MCAST_END_STREAM, STREAM_SCRATCH_5_REG_INDEX)))
#endif

// Indexed with operand = kernel operand ID (0-31) per the table above
// Used for tile push/pop operations. 
inline __attribute__((always_inline)) uint32_t get_operand_stream_id(int operand) {
// Does not fit in grayskull firmware
//#ifdef TENSIX_FIRMWARE
//  if (*MCAST_PACKER_OPT_EN && operand >= OPERAND_OUTPUT_START_INDEX && operand < OPERAND_INTERMEDIATES_START_INDEX) {
//    return MCAST_END_STREAM - (operand - OPERAND_OUTPUT_START_INDEX);
//  }
//#endif

  return OPERAND_START_STREAM + operand;
}

inline __attribute__((always_inline)) int stream_id_to_operand(uint32_t stream_id) {
// Does not fit in grayskull firmware
//#ifdef TENSIX_FIRMWARE
//  if (stream_id <= MCAST_END_STREAM) {
//    return OPERAND_OUTPUT_START_INDEX + (MCAST_END_STREAM - stream_id);
//  }
//#endif

  return stream_id - OPERAND_START_STREAM;
}

inline __attribute__((always_inline)) int stream_id_to_output(uint32_t stream_id) {
// Does not fit in grayskull firmware
//#ifdef TENSIX_FIRMWARE
//  if (*MCAST_PACKER_OPT_EN && stream_id >= MCAST_START_STREAM && stream_id <= MCAST_END_STREAM) {
//    return MCAST_END_STREAM - stream_id;
//  }
//#endif

  return (stream_id - OUTPUT_START_STREAM);
}

// This should only be used by llk tb, this is meant only as a hack
inline __attribute__((always_inline)) uint32_t old_stream_id_to_new_stream_id(uint32_t stream_id) {
  return stream_id;
}

// Functions below convert between kernel operand indexes (per the above table)
// and input/output indexes that can be used to iterate separately through
// streams that have kernel input (stream->unpacker) or kernel output
// (packer->stream) functionality. 
inline __attribute__((always_inline)) int operand_to_input_index(int operand) {
  return (operand >= OPERAND_INTERMEDIATES_START_INDEX) ? (operand - (OPERAND_INTERMEDIATES_START_INDEX - OPERAND_OUTPUT_START_INDEX)) : operand;
}

inline __attribute__((always_inline)) int input_to_operand_index(int input) {
  return (input >= OPERAND_OUTPUT_START_INDEX) ? (input + (OPERAND_INTERMEDIATES_START_INDEX - OPERAND_OUTPUT_START_INDEX)) : input;
}

inline __attribute__((always_inline)) int operand_to_output_index(int operand) {
  return operand - OPERAND_OUTPUT_START_INDEX;
}

inline __attribute__((always_inline)) int output_to_operand_index(int output) {
  return output + OPERAND_OUTPUT_START_INDEX;
}

inline __attribute__((always_inline)) bool operand_is_intermediate(int operand) {
  return (operand>=OPERAND_INTERMEDIATES_START_INDEX);
}


// Pointers to scratch registers (implemented using don't-care functional registers) for input
// stream tile sync operations:
#ifdef TENSIX_FIRMWARE

// XXXXX FIXME: separate interface for use by pipegen and loader from
// implementation below for firmware

#ifdef PERF_DUMP

  // Must be the same values as in perf_lib/perf_base.hpp
  static constexpr uint8_t PERF_MAX_NUM_INPUTS = 8;
  static constexpr uint8_t PERF_MAX_NUM_OUTPUTS = 1;

  #define PERF_RISC_MAILBOX_INPUT_DECOUPLE_MASK_PTR  ((volatile uint32_t tt_l1_ptr *) (l1_mem::address_map::PERF_RISC_MAILBOX_ADDR))
  #define PERF_RISC_MAILBOX_OUTPUT_DECOUPLE_MASK_PTR ((volatile uint32_t tt_l1_ptr *) (l1_mem::address_map::PERF_RISC_MAILBOX_ADDR + 4))
  #define PERF_DRAM_BUFFER_RESET_MAILBOX_PTR         ((volatile uint32_t tt_l1_ptr *) (l1_mem::address_map::PERF_RESET_PTR_MAILBOX_ADDR))

  #if OVERLAY_DECOUPLE == 1
  #define PERF_ANALYZER_COMMAND_START_PTR            ((volatile uint32_t tt_l1_ptr *) (l1_mem::address_map::PERF_ANALYZER_COMMAND_START_PTR_ADDR))
  #define PERF_ANALYZER_COMMAND_START_VAL            ((volatile uint32_t tt_l1_ptr *) (l1_mem::address_map::PERF_ANALYZER_COMMAND_START_VAL_ADDR))

  inline __attribute__((always_inline)) bool is_input_operand_decoupled(int operand) {
    if (operand >= OPERAND_INPUT_PARAMS_START_INDEX) {
        return false;
    }
    uint32_t overlay_input_decouple_mask = *PERF_RISC_MAILBOX_INPUT_DECOUPLE_MASK_PTR;
    const uint8_t operand_mask = 1 << (operand & 0xff);
    return (overlay_input_decouple_mask & 0xff) & operand_mask;
  }

  inline __attribute__((always_inline)) bool is_output_operand_decoupled(int operand, uint8_t overlay_output_decouple_mask) {
    if (operand < OPERAND_OUTPUT_START_INDEX || operand >= OPERAND_INTERMEDIATES_START_INDEX) {
        return false;
    }
    const uint8_t operand_mask = 1 << ((operand-OPERAND_OUTPUT_START_INDEX) & 0xff);
    return overlay_output_decouple_mask & operand_mask;
  }

  #endif
#endif

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_operand_tiles_received_ptr(int operand) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX));
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_operand_tiles_acked_ptr(int operand) {
#if defined(PERF_DUMP) && (OVERLAY_DECOUPLE == 1)
  if (is_input_operand_decoupled(operand)) {
    return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX));
  } else {
    return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_DEST_BUF_START_REG_INDEX));
  }
#else
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_DEST_BUF_START_REG_INDEX));
#endif
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_operand_phase_changed_ptr(int operand) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX));
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_packer_tiles_received_ptr(int operand) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_SRC_PHASE_REG_INDEX));
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_packer_tiles_acked_ptr(int operand) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(get_operand_stream_id(operand), STREAM_REMOTE_SRC_REG_INDEX));
}

// Datacopy optimization functions
inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_operand_stream_tiles_received_ptr(uint32_t stream_id) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(stream_id, STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX));
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_operand_stream_tiles_acked_ptr(uint32_t stream_id) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(stream_id, STREAM_REMOTE_DEST_BUF_START_REG_INDEX));
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_packer_stream_tiles_received_ptr(uint32_t stream_id) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(stream_id, STREAM_REMOTE_SRC_PHASE_REG_INDEX));
}

inline __attribute__((always_inline)) volatile uint32_t * tt_reg_ptr get_packer_stream_tiles_acked_ptr(uint32_t stream_id) {
  return (volatile uint32_t tt_reg_ptr *)(uintptr_t)(STREAM_REG_ADDR(stream_id, STREAM_REMOTE_SRC_REG_INDEX));
}

#endif

#endif
