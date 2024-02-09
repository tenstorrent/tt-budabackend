// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _RISC_COMMON_H_
#define _RISC_COMMON_H_

#include <stdint.h>

#include "noc_parameters.h"
#include "tensix.h"
#include "risc.h"
#include "risc_attribs.h"

#define NOC_X(x) (loading_noc == 0 ? (x) : (noc_size_x-1-(x)))
#define NOC_Y(y) (loading_noc == 0 ? (y) : (noc_size_y-1-(y)))

#define DRAM_PTR_UPDATE_VC 1

#define TILE_WORD_2_BIT ((256 + 64 + 32) >> 4)
#define TILE_WORD_4_BIT ((512 + 64 + 32) >> 4)
#define TILE_WORD_8_BIT ((32*32*1 + 64 + 32) >> 4)
#define TILE_WORD_16_BIT ((32*32*2 + 32) >> 4)
#define TILE_WORD_32_BIT ((32*32*4 + 32) >> 4)

#define RISC_LOCAL_DATA_MEM_BASE 0xFFB00000

#define RISC_DETECTED_STREAM_ASSERT 0xdeeeaaad

const uint32_t STREAM_RESTART_CHECK_MASK = (0x1 << 3) - 1;

const uint32_t MAX_TILES_PER_PHASE = 2048;

extern uint8_t my_x[NUM_NOCS];
extern uint8_t my_y[NUM_NOCS];
extern uint8_t my_logical_x[NUM_NOCS];
extern uint8_t my_logical_y[NUM_NOCS];
extern uint8_t loading_noc;
extern uint8_t noc_size_x;
extern uint8_t noc_size_y;
extern uint8_t noc_trans_table_en;
extern volatile uint32_t local_mem_barrier;

inline void WRITE_REG(uint32_t addr, uint32_t val) {
  volatile uint32_t* ptr = (volatile uint32_t*)addr;
  ptr[0] = val;
}

inline uint32_t READ_REG(uint32_t addr) {
  volatile uint32_t tt_reg_ptr * ptr = (volatile uint32_t tt_reg_ptr *)addr;
  return ptr[0];
}

extern int post_index;

inline uint32_t dram_io_incr_ptr(uint32_t curr_ptr, uint32_t incr, uint32_t buf_size_q_slots) {
  uint32_t next_ptr = curr_ptr + incr;
  uint32_t double_buf_size_q_slots = 2*buf_size_q_slots;
  if (next_ptr >= double_buf_size_q_slots) {
    next_ptr -= double_buf_size_q_slots;
  }
  return next_ptr;
}

inline __attribute__((always_inline)) uint32_t dram_io_incr_ptr_l1(uint32_t curr_ptr, uint32_t incr, uint32_t buf_size_q_slots) {
  uint32_t next_ptr = curr_ptr + incr;
  uint32_t double_buf_size_q_slots = 2*buf_size_q_slots;
  if (next_ptr >= double_buf_size_q_slots) {
    next_ptr -= double_buf_size_q_slots;
  }
  return next_ptr;
}

inline __attribute__((always_inline)) uint32_t dram_io_empty(uint32_t rd_ptr, uint32_t wr_ptr) {
  return (rd_ptr == wr_ptr);
}

inline __attribute__((always_inline)) uint32_t dram_io_local_empty(uint32_t local_rd_ptr, uint32_t rd_ptr, uint32_t wr_ptr) {
  if (rd_ptr == wr_ptr)
    return true;

  uint32_t case1 = rd_ptr < wr_ptr && (local_rd_ptr < rd_ptr || local_rd_ptr >= wr_ptr);
  uint32_t case2 = rd_ptr > wr_ptr && wr_ptr <= local_rd_ptr && local_rd_ptr < rd_ptr;
  
  return case1 || case2;
}

inline uint32_t dram_io_full(uint32_t rd_ptr, uint32_t wr_ptr, uint32_t buf_size_q_slots) {
  uint32_t wr_ptr_reduced_by_q_slots = wr_ptr - buf_size_q_slots;
  uint32_t rd_ptr_reduced_by_q_slots = rd_ptr - buf_size_q_slots;
  uint32_t case1 = (wr_ptr_reduced_by_q_slots == rd_ptr);
  uint32_t case2 = (rd_ptr_reduced_by_q_slots == wr_ptr);
  return case1 || case2;
}

inline __attribute__((always_inline)) uint32_t dram_io_entries(uint32_t rd_ptr, uint32_t wr_ptr, uint32_t buf_size_q_slots) {
  if (wr_ptr >= rd_ptr)
    return wr_ptr - rd_ptr;
  else
    return 2*buf_size_q_slots - rd_ptr + wr_ptr;
}

inline __attribute__((always_inline)) uint32_t buf_ptr_inc_wrap(uint32_t buf_ptr, uint32_t inc, uint32_t buf_size) {
  uint32_t result = buf_ptr + inc;
  if (result >= buf_size) {
    result -= buf_size;
  }
  return result;
}

inline __attribute__((always_inline)) uint32_t buf_ptr_dec_wrap(uint32_t buf_ptr, uint32_t dec, uint32_t buf_size) {
  uint32_t result = buf_ptr;
  if (dec > result) {
    result += buf_size;
  }
  result -= dec;
  return result;
}

inline uint32_t reg_read_barrier(uint32_t addr)
{
    volatile uint32_t *p_reg = reinterpret_cast<volatile uint32_t *> (addr);
    uint32_t data = p_reg[0];
    local_mem_barrier = data;
    return data;
}

inline __attribute__((section("code_l1"))) uint32_t reg_read_barrier_l1(uint32_t addr)
{
    volatile uint32_t *p_reg = reinterpret_cast<volatile uint32_t *> (addr);
    uint32_t data = p_reg[0];
    local_mem_barrier = data;
    return data;
}

inline void assert_trisc_reset() {
  uint32_t soft_reset_0 = READ_REG(RISCV_DEBUG_REG_SOFT_RESET_0);
  uint32_t trisc_reset_mask = 0x7000;
  WRITE_REG(RISCV_DEBUG_REG_SOFT_RESET_0, soft_reset_0 | trisc_reset_mask);
}


inline void deassert_trisc_reset() {
  uint32_t soft_reset_0 = READ_REG(RISCV_DEBUG_REG_SOFT_RESET_0);
  uint32_t trisc_reset_mask = 0x7000;
  WRITE_REG(RISCV_DEBUG_REG_SOFT_RESET_0, soft_reset_0 & ~trisc_reset_mask);
}

inline uint32_t special_mult(uint32_t a, uint32_t special_b) {
  if (special_b == TILE_WORD_8_BIT)
    return a * TILE_WORD_8_BIT;
  else if (special_b == TILE_WORD_16_BIT)
    return a * TILE_WORD_16_BIT;
  else if (special_b == TILE_WORD_4_BIT)
    return a * TILE_WORD_4_BIT;
  else if (special_b == TILE_WORD_2_BIT)
    return a * TILE_WORD_2_BIT;
  else if (special_b == TILE_WORD_32_BIT)
    return a * TILE_WORD_32_BIT;
  
  RISC_POST_STATUS(0xDEAD0002);
  while(true);
  return 0;
}

inline __attribute__((always_inline)) unsigned int mulsi3 (unsigned int a, unsigned int b)
{
  unsigned int r = 0;
  while (a)
    {
      if (a & 1)
        r += b;
      a >>= 1;
      b <<= 1;
    }
  return r;
}

// In gs we should use call_with_cpu_flush((void *)address, 0); instead of call_function(address);
inline __attribute__((always_inline)) void call_function(uint32_t address)
{
  ((void (*)(void))address)();
}

void risc_reset_check();
#ifdef GRAYSKULL
void risc_init();
#else
#ifdef RISC_B0_HW
void __attribute__((section("code_l1"))) risc_init();
#else
void risc_init();
#endif
#endif
void tile_header_buffer_init();

// This call blocks until NCRISC indicates that all epoch start state
// has been loaded from DRAM to L1. 
void risc_get_next_epoch();
// This call signals to NCRISC that the current epoch is done and can
// be overwritten with the next epoch state from DRAM. 
void risc_signal_epoch_done();

#ifdef RISC_GSYNC_ENABLED
void global_sync_init(volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress);
void global_sync(volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress)__attribute__((section("code_l1")));
void global_sync_update(volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress);
#endif


inline __attribute__ ((always_inline)) void disable_lowcache() {
#ifdef RISC_BLACKHOLE_HW
  // asm(R"ASM( 
  //         csrrsi zero, 0x7c0, 0x8 
  //       )ASM"); 
  asm(R"ASM(
        .option push
        li   t1, 0x1
        slli t1, t1, 3
        csrrs zero, 0x7c0, t1
        .option pop
         )ASM"
    :::"t1");
#endif
}

#endif

