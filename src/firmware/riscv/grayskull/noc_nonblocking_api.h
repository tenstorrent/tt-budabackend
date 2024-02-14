// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include "noc_parameters.h"
#include "risc_attribs.h"

////

const uint32_t NCRISC_WR_CMD_BUF = 0;
const uint32_t NCRISC_WR_CMD_BUF_0 = NCRISC_WR_CMD_BUF;
const uint32_t NCRISC_WR_CMD_BUF_1 = 1;
const uint32_t NCRISC_WR_CMD_BUF_2 = 2;
const uint32_t NCRISC_SMALL_TXN_CMD_BUF = 3;
//const uint32_t NCRISC_RD_CMD_BUF = NCRISC_SMALL_TXN_CMD_BUF;
//const uint32_t NCRISC_WR_REG_CMD_BUF = NCRISC_SMALL_TXN_CMD_BUF;
//const uint32_t NCRISC_AT_CMD_BUF = NCRISC_SMALL_TXN_CMD_BUF;

const uint32_t NCRISC_WR_DEF_TRID = 0;
const uint32_t NCRISC_WR_LOCAL_TRID = 0;
const uint32_t NCRISC_RD_DEF_TRID = 0;
const uint32_t NCRISC_HEADER_RD_TRID = 0;
const uint32_t NCRISC_RD_START_TRID = 0;
const uint32_t NCRISC_RD_END_TRID = 0;
const uint32_t NCRISC_ETH_START_TRID = 0;
const uint32_t NCRISC_ETH_END_TRID = 0;

extern uint32_t noc_reads_num_issued[NUM_NOCS];
extern uint32_t noc_nonposted_writes_num_issued[NUM_NOCS];
extern uint32_t noc_nonposted_writes_acked[NUM_NOCS];
extern uint32_t noc_xy_local_addr[NUM_NOCS];

inline void NOC_CMD_BUF_WRITE_REG(uint32_t noc, uint32_t buf, uint32_t addr, uint32_t val) {
  uint32_t offset = (buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT) + addr;
  volatile uint32_t* ptr = (volatile uint32_t*)offset;
  *ptr = val;
}


inline uint32_t NOC_CMD_BUF_READ_REG(uint32_t noc, uint32_t buf, uint32_t addr) {
  uint32_t offset = (buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT) + addr;
  volatile uint32_t tt_reg_ptr * ptr = (volatile uint32_t tt_reg_ptr *)offset;
  return *ptr;
}


inline uint32_t NOC_STATUS_READ_REG(uint32_t noc, uint32_t reg_id) {
  uint32_t offset = (noc << NOC_INSTANCE_OFFSET_BIT) + NOC_STATUS(reg_id);
  volatile uint32_t tt_reg_ptr * ptr = (volatile uint32_t tt_reg_ptr *)offset;
  return *ptr;
}


inline __attribute__((section("code_l1"))) void NOC_CMD_BUF_WRITE_REG_L1(uint32_t noc, uint32_t buf, uint32_t addr, uint32_t val) {
  uint32_t offset = (buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT) + addr;
  volatile uint32_t* ptr = (volatile uint32_t*)offset;
  *ptr = val;
}


inline __attribute__((section("code_l1"))) uint32_t NOC_CMD_BUF_READ_REG_L1(uint32_t noc, uint32_t buf, uint32_t addr) {
  uint32_t offset = (buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT) + addr;
  volatile uint32_t tt_reg_ptr * ptr = (volatile uint32_t tt_reg_ptr *)offset;
  return *ptr;
}


inline __attribute__((section("code_l1"))) uint32_t NOC_STATUS_READ_REG_L1(uint32_t noc, uint32_t reg_id) {
  uint32_t offset = (noc << NOC_INSTANCE_OFFSET_BIT) + NOC_STATUS(reg_id);
  volatile uint32_t tt_reg_ptr * ptr = (volatile uint32_t tt_reg_ptr *)offset;
  return *ptr;
}


inline void ncrisc_noc_fast_read(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  if (len_bytes > 0) {
    //word offset noc cmd interface
    uint32_t noc_rd_cmd_field = NOC_CMD_CPY | NOC_CMD_RD | NOC_CMD_RESP_MARKED | NOC_CMD_VC_STATIC | NOC_CMD_STATIC_VC(1);
    uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
    volatile uint32_t* ptr = (volatile uint32_t*)offset;

    ptr[NOC_RET_ADDR_LO >> 2] = dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_CTRL >> 2] = noc_rd_cmd_field;
    ptr[NOC_TARG_ADDR_LO >> 2] = (uint32_t)src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = src_addr >> 32;
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;
    noc_reads_num_issued[noc] += 1;
  }
}

inline __attribute__((always_inline)) void ncrisc_noc_fast_read_scatter(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  if (len_bytes > 0) {
    //word offset noc cmd interface
    uint32_t noc_rd_cmd_field = NOC_CMD_CPY | NOC_CMD_RD | NOC_CMD_RESP_MARKED | NOC_CMD_VC_STATIC | NOC_CMD_STATIC_VC(1);
    uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
    volatile uint32_t* ptr = (volatile uint32_t*)offset;

    ptr[NOC_RET_ADDR_LO >> 2] = dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_CTRL >> 2] = noc_rd_cmd_field;
    ptr[NOC_TARG_ADDR_LO >> 2] = (uint32_t)src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = src_addr >> 32;
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;
    noc_reads_num_issued[noc] += 1;
  }
}


inline __attribute__((always_inline)) __attribute__((section("code_l1"))) void ncrisc_noc_fast_read_l1(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  if (len_bytes > 0) {
    //word offset noc cmd interface
    uint32_t noc_rd_cmd_field = NOC_CMD_CPY | NOC_CMD_RD | NOC_CMD_RESP_MARKED | NOC_CMD_VC_STATIC | NOC_CMD_STATIC_VC(1);
    uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
    volatile uint32_t* ptr = (volatile uint32_t*)offset;

    ptr[NOC_RET_ADDR_LO >> 2] = dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_CTRL >> 2] = noc_rd_cmd_field;
    ptr[NOC_TARG_ADDR_LO >> 2] = (uint32_t)src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = src_addr >> 32;
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;
    noc_reads_num_issued[noc] += 1;
  }
}

inline bool ncrisc_noc_reads_flushed(uint32_t noc, uint32_t transaction_id) {
  return (NOC_STATUS_READ_REG(noc, NIU_MST_RD_RESP_RECEIVED) == noc_reads_num_issued[noc]);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_reads_flushed_l1(uint32_t noc, uint32_t transaction_id) {
  return (NOC_STATUS_READ_REG_L1(noc, NIU_MST_RD_RESP_RECEIVED) == noc_reads_num_issued[noc]);
}

inline bool ncrisc_noc_all_reads_flushed(uint32_t noc) {
  return ncrisc_noc_reads_flushed(noc, NCRISC_RD_DEF_TRID);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_all_reads_flushed_l1(uint32_t noc) {
  return ncrisc_noc_reads_flushed_l1(noc, NCRISC_RD_DEF_TRID);
}

inline bool ncrisc_noc_fast_read_ok(uint32_t noc, uint32_t cmd_buf) {
  return (NOC_CMD_BUF_READ_REG(noc, cmd_buf, NOC_CMD_CTRL) == NOC_CTRL_STATUS_READY);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_fast_read_ok_l1(uint32_t noc, uint32_t cmd_buf) {
  return (NOC_CMD_BUF_READ_REG_L1(noc, cmd_buf, NOC_CMD_CTRL) == NOC_CTRL_STATUS_READY);
}

inline __attribute__((always_inline)) uint32_t ncrisc_rd_data_word_recv(uint32_t noc) {
  return NOC_STATUS_READ_REG(noc, NIU_MST_RD_DATA_WORD_RECEIVED);
}


inline void ncrisc_noc_fast_write(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, bool mcast, bool linked, uint32_t num_dests, uint32_t transaction_id) {
  if (len_bytes > 0) {
    uint32_t noc_cmd_field =
      NOC_CMD_CPY | NOC_CMD_WR |
      NOC_CMD_VC_STATIC  |
      NOC_CMD_STATIC_VC(vc) | 
      (linked ? NOC_CMD_VC_LINKED : 0x0) |
      (mcast ? (NOC_CMD_PATH_RESERVE | NOC_CMD_BRCST_PACKET) : 0x0) |
      NOC_CMD_RESP_MARKED;

    //word offset noc cmd interface
    uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
    volatile uint32_t* ptr = (volatile uint32_t*)offset;
    ptr[NOC_CTRL >> 2] = noc_cmd_field;
    ptr[NOC_TARG_ADDR_LO >> 2] = src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_RET_ADDR_LO >> 2] = (uint32_t)dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = dest_addr >> 32;
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;

    noc_nonposted_writes_num_issued[noc] += 1;
    noc_nonposted_writes_acked[noc] += num_dests;
  }
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) void ncrisc_noc_fast_write_l1(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, bool mcast, bool linked, uint32_t num_dests, uint32_t transaction_id) {
  if (len_bytes > 0) {
    uint32_t noc_cmd_field =
      NOC_CMD_CPY | NOC_CMD_WR |
      NOC_CMD_VC_STATIC  |
      NOC_CMD_STATIC_VC(vc) |
      (linked ? NOC_CMD_VC_LINKED : 0x0) |
      (mcast ? (NOC_CMD_PATH_RESERVE | NOC_CMD_BRCST_PACKET) : 0x0) |
      NOC_CMD_RESP_MARKED;

    //word offset noc cmd interface
    uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
    volatile uint32_t* ptr = (volatile uint32_t*)offset;
    ptr[NOC_CTRL >> 2] = noc_cmd_field;
    ptr[NOC_TARG_ADDR_LO >> 2] = src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_RET_ADDR_LO >> 2] = (uint32_t)dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = dest_addr >> 32;
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;

    noc_nonposted_writes_num_issued[noc] += 1;
    noc_nonposted_writes_acked[noc] += num_dests;
  }
}

inline bool ncrisc_noc_fast_write_ok(uint32_t noc, uint32_t cmd_buf) {
  return (NOC_CMD_BUF_READ_REG(noc, cmd_buf, NOC_CMD_CTRL) == NOC_CTRL_STATUS_READY);
}

inline bool ncrisc_noc_fast_write_bufs_ok(uint32_t noc) {
  //word offset between cmd buffers
  uint32_t cmd_buf_offset = 0x1 << (NOC_CMD_BUF_OFFSET_BIT - 2);
  uint32_t offset = (noc << NOC_INSTANCE_OFFSET_BIT) + NOC_CMD_CTRL;
  uint32_t* ptr = (uint32_t*)offset;

  uint32_t a = ptr[0];
  ptr += cmd_buf_offset;
  uint32_t ok = a;
  uint32_t b = ptr[0];
  ptr += cmd_buf_offset;
  ok += b;
  uint32_t c = ptr[0];
  ok += c;

  return (ok == NOC_CTRL_STATUS_READY);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_fast_write_ok_l1(uint32_t noc, uint32_t cmd_buf) {
  return (NOC_CMD_BUF_READ_REG_L1(noc, cmd_buf, NOC_CMD_CTRL) == NOC_CTRL_STATUS_READY);
}

inline void ncrisc_noc_blitz_write_setup(uint32_t noc, uint32_t cmd_buf, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, uint32_t num_times_to_write, uint32_t transaction_id) {
  uint32_t noc_cmd_field =
    NOC_CMD_CPY | NOC_CMD_WR |
    NOC_CMD_VC_STATIC  |
    NOC_CMD_STATIC_VC(vc) |
    NOC_CMD_RESP_MARKED;
     
  while (!ncrisc_noc_fast_write_ok(noc, cmd_buf)); 
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_CTRL, noc_cmd_field);
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_AT_LEN_BE, len_bytes);
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_RET_ADDR_MID, dest_addr >> 32);
  noc_nonposted_writes_num_issued[noc] += num_times_to_write;
  noc_nonposted_writes_acked[noc] += num_times_to_write;
}

inline __attribute__((always_inline)) void ncrisc_noc_blitz_write(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint32_t dest_addr) {
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_TARG_ADDR_LO, src_addr);
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_RET_ADDR_LO, dest_addr);
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_CMD_CTRL, NOC_CTRL_SEND_REQ);
}

inline bool ncrisc_noc_nonposted_writes_sent(uint32_t noc, uint32_t transaction_id) {
  return (NOC_STATUS_READ_REG(noc, NIU_MST_NONPOSTED_WR_REQ_SENT) == noc_nonposted_writes_num_issued[noc]);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_nonposted_writes_sent_l1(uint32_t noc, uint32_t transaction_id) {
  return (NOC_STATUS_READ_REG_L1(noc, NIU_MST_NONPOSTED_WR_REQ_SENT) == noc_nonposted_writes_num_issued[noc]);
}

inline bool ncrisc_noc_nonposted_all_writes_sent(uint32_t noc) {
  return ncrisc_noc_nonposted_writes_sent(noc, NCRISC_WR_DEF_TRID);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_nonposted_all_writes_sent_l1(uint32_t noc) {
  return ncrisc_noc_nonposted_writes_sent_l1(noc, NCRISC_WR_DEF_TRID);
}

inline bool ncrisc_noc_nonposted_writes_flushed(uint32_t noc, uint32_t transaction_id) {
  return (NOC_STATUS_READ_REG(noc, NIU_MST_WR_ACK_RECEIVED) == noc_nonposted_writes_acked[noc]);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_nonposted_writes_flushed_l1(uint32_t noc, uint32_t transaction_id) {
  return (NOC_STATUS_READ_REG_L1(noc, NIU_MST_WR_ACK_RECEIVED) == noc_nonposted_writes_acked[noc]);
}

inline bool ncrisc_noc_nonposted_all_writes_flushed(uint32_t noc) {
  return ncrisc_noc_nonposted_writes_flushed(noc, NCRISC_WR_DEF_TRID);
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) bool ncrisc_noc_nonposted_all_writes_flushed_l1(uint32_t noc) {
  return ncrisc_noc_nonposted_writes_flushed_l1(noc, NCRISC_WR_DEF_TRID);
}


inline void ncrisc_noc_init() {
  for (int noc = 0; noc < NUM_NOCS; noc++) {
    uint32_t noc_id_reg = NOC_CMD_BUF_READ_REG(noc, 0, NOC_NODE_ID);
    uint32_t my_x = noc_id_reg & NOC_NODE_ID_MASK;
    uint32_t my_y = (noc_id_reg >> NOC_ADDR_NODE_ID_BITS) & NOC_NODE_ID_MASK;
    uint64_t xy_local_addr = NOC_XY_ADDR(my_x, my_y, 0);
    noc_xy_local_addr[noc] = (uint32_t)(xy_local_addr >> 32);
    
    NOC_CMD_BUF_WRITE_REG(noc, NCRISC_WR_CMD_BUF_0, NOC_TARG_ADDR_MID, (uint32_t)(xy_local_addr >> 32));
    NOC_CMD_BUF_WRITE_REG(noc, NCRISC_WR_CMD_BUF_1, NOC_TARG_ADDR_MID, (uint32_t)(xy_local_addr >> 32));
    NOC_CMD_BUF_WRITE_REG(noc, NCRISC_WR_CMD_BUF_2, NOC_TARG_ADDR_MID, (uint32_t)(xy_local_addr >> 32));

    //NOC_CMD_BUF_WRITE_REG(noc, NCRISC_WR_REG_CMD_BUF, NOC_TARG_ADDR_MID, (uint32_t)(xy_local_addr >> 32));

    //uint32_t noc_rd_cmd_field = NOC_CMD_CPY | NOC_CMD_RD | NOC_CMD_RESP_MARKED | NOC_CMD_VC_STATIC | NOC_CMD_STATIC_VC(1);
  
    //NOC_CMD_BUF_WRITE_REG(noc, NCRISC_RD_CMD_BUF, NOC_CTRL, noc_rd_cmd_field);
    //NOC_CMD_BUF_WRITE_REG(noc, NCRISC_RD_CMD_BUF, NOC_RET_ADDR_MID, (uint32_t)(xy_local_addr >> 32)); 
  
    noc_reads_num_issued[noc] = NOC_STATUS_READ_REG(noc, NIU_MST_RD_RESP_RECEIVED);
    noc_nonposted_writes_num_issued[noc] = NOC_STATUS_READ_REG(noc, NIU_MST_NONPOSTED_WR_REQ_SENT);
    noc_nonposted_writes_acked[noc] = NOC_STATUS_READ_REG(noc, NIU_MST_WR_ACK_RECEIVED);
  }
}

inline void ncrisc_noc_counters_init() {
  for (int noc = 0; noc < NUM_NOCS; noc++) {
    noc_reads_num_issued[noc] = NOC_STATUS_READ_REG(noc, NIU_MST_RD_RESP_RECEIVED);
    noc_nonposted_writes_num_issued[noc] = NOC_STATUS_READ_REG(noc, NIU_MST_NONPOSTED_WR_REQ_SENT);
    noc_nonposted_writes_acked[noc] = NOC_STATUS_READ_REG(noc, NIU_MST_WR_ACK_RECEIVED);
  }
}

inline bool ncrisc_noc_all_flushed(uint32_t noc) {
  return ncrisc_noc_reads_flushed(noc, NCRISC_RD_DEF_TRID) && ncrisc_noc_nonposted_writes_sent(noc, NCRISC_WR_DEF_TRID) && ncrisc_noc_nonposted_writes_flushed(noc, NCRISC_WR_DEF_TRID);
}

inline void ncrisc_noc_full_sync() {
  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    while (!ncrisc_noc_reads_flushed(n, NCRISC_RD_DEF_TRID));
    while (!ncrisc_noc_nonposted_writes_sent(n, NCRISC_WR_DEF_TRID));
    while (!ncrisc_noc_nonposted_writes_flushed(n, NCRISC_WR_DEF_TRID));
  }
}

inline void ncrisc_noc_fast_read_any_len(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  while (len_bytes > NOC_MAX_BURST_SIZE) {              
    while (!ncrisc_noc_fast_read_ok(noc, cmd_buf));                       
    ncrisc_noc_fast_read(noc, cmd_buf, src_addr, dest_addr, NOC_MAX_BURST_SIZE, transaction_id); 
    src_addr += NOC_MAX_BURST_SIZE;                
    dest_addr += NOC_MAX_BURST_SIZE;               
    len_bytes -= NOC_MAX_BURST_SIZE;                    
  }                                                                    
  while (!ncrisc_noc_fast_read_ok(noc, cmd_buf));                                  
  ncrisc_noc_fast_read(noc, cmd_buf, src_addr, dest_addr, len_bytes, transaction_id); 
}

inline __attribute__((always_inline)) void ncrisc_noc_fast_read_any_len_scatter(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  while (len_bytes > NOC_MAX_BURST_SIZE) {              
    while (!ncrisc_noc_fast_read_ok(noc, cmd_buf));                       
    ncrisc_noc_fast_read_scatter(noc, cmd_buf, src_addr, dest_addr, NOC_MAX_BURST_SIZE, transaction_id); 
    src_addr += NOC_MAX_BURST_SIZE;                
    dest_addr += NOC_MAX_BURST_SIZE;               
    len_bytes -= NOC_MAX_BURST_SIZE;                    
  }                                                                    
  while (!ncrisc_noc_fast_read_ok(noc, cmd_buf));                                  
  ncrisc_noc_fast_read_scatter(noc, cmd_buf, src_addr, dest_addr, len_bytes, transaction_id); 
}


inline __attribute__((always_inline)) __attribute__((section("code_l1"))) void ncrisc_noc_fast_read_any_len_l1(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  while (len_bytes > NOC_MAX_BURST_SIZE) {              
    while (!ncrisc_noc_fast_read_ok_l1(noc, cmd_buf));                       
    ncrisc_noc_fast_read_l1(noc, cmd_buf, src_addr, dest_addr, NOC_MAX_BURST_SIZE, transaction_id); 
    src_addr += NOC_MAX_BURST_SIZE;                
    dest_addr += NOC_MAX_BURST_SIZE;               
    len_bytes -= NOC_MAX_BURST_SIZE;                    
  }                                                                    
  while (!ncrisc_noc_fast_read_ok_l1(noc, cmd_buf));                                  
  ncrisc_noc_fast_read_l1(noc, cmd_buf, src_addr, dest_addr, len_bytes, transaction_id); 
}

 
inline void ncrisc_noc_fast_write_any_len(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, bool mcast, bool linked, uint32_t num_dests, uint32_t transaction_id) {
  while (len_bytes > NOC_MAX_BURST_SIZE) {              
    while (!ncrisc_noc_fast_write_ok(noc, cmd_buf));                                
    ncrisc_noc_fast_write(noc, cmd_buf, src_addr, dest_addr, NOC_MAX_BURST_SIZE, vc, mcast, linked, num_dests, transaction_id); 
    src_addr += NOC_MAX_BURST_SIZE;                
    dest_addr += NOC_MAX_BURST_SIZE;               
    len_bytes -= NOC_MAX_BURST_SIZE;                    
    if (!ncrisc_noc_fast_write_ok(noc, cmd_buf)) {
      cmd_buf++;
      if (cmd_buf == 3) cmd_buf = 0;
    }
  }                                                                    
  while (!ncrisc_noc_fast_write_ok(noc, cmd_buf));                                  
  ncrisc_noc_fast_write(noc, cmd_buf, src_addr, dest_addr, len_bytes, vc, mcast, linked, num_dests, transaction_id); 
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) void ncrisc_noc_fast_write_any_len_l1(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, bool mcast, bool linked, uint32_t num_dests, uint32_t transaction_id) {
  while (len_bytes > NOC_MAX_BURST_SIZE) {
    while (!ncrisc_noc_fast_write_ok_l1(noc, cmd_buf));
    ncrisc_noc_fast_write_l1(noc, cmd_buf, src_addr, dest_addr, NOC_MAX_BURST_SIZE, vc, mcast, linked, num_dests, transaction_id);
    src_addr += NOC_MAX_BURST_SIZE;
    dest_addr += NOC_MAX_BURST_SIZE;
    len_bytes -= NOC_MAX_BURST_SIZE;
    if (!ncrisc_noc_fast_write_ok_l1(noc, cmd_buf)) {
      cmd_buf++;
      if (cmd_buf == 3) cmd_buf = 0;
    }
  }
  while (!ncrisc_noc_fast_write_ok_l1(noc, cmd_buf));
  ncrisc_noc_fast_write_l1(noc, cmd_buf, src_addr, dest_addr, len_bytes, vc, mcast, linked, num_dests, transaction_id);
}

inline void noc_atomic_read_and_increment(uint32_t noc, uint32_t cmd_buf, uint64_t addr, uint32_t incr, uint32_t wrap, uint64_t read_addr, bool linked, uint32_t transaction_id) {

  uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
  volatile uint32_t* ptr = (volatile uint32_t*)offset;
  uint32_t atomic_resp = NOC_STATUS_READ_REG(noc, NIU_MST_ATOMIC_RESP_RECEIVED);

  ptr[NOC_TARG_ADDR_LO >> 2] = (uint32_t)(addr & 0xFFFFFFFF);
  ptr[NOC_TARG_ADDR_MID >> 2] = addr >> 32;
  //ptr[NOC_PACKET_TAG >> 2] = NOC_PACKET_TAG_TRANSACTION_ID(transaction_id);
  ptr[NOC_RET_ADDR_LO >> 2] = (uint32_t)(read_addr & 0xFFFFFFFF);
  ptr[NOC_RET_ADDR_MID >> 2] = (uint32_t)(read_addr >> 32);
  ptr[NOC_CTRL >> 2] = (linked ? NOC_CMD_VC_LINKED : 0x0) |
                       NOC_CMD_AT |
                       NOC_CMD_RESP_MARKED;
  ptr[NOC_AT_LEN_BE >> 2] = NOC_AT_INS(NOC_AT_INS_INCR_GET) | NOC_AT_WRAP(wrap) | NOC_AT_IND_32((addr>>2) & 0x3) | NOC_AT_IND_32_SRC(0);
  ptr[NOC_AT_DATA >> 2] = incr;
  ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;

  atomic_resp++;
  while (atomic_resp != NOC_STATUS_READ_REG(noc, NIU_MST_ATOMIC_RESP_RECEIVED));
}

inline __attribute__((always_inline)) __attribute__((section("code_l1"))) void noc_atomic_read_and_increment_l1(uint32_t noc, uint32_t cmd_buf, uint64_t addr, uint32_t incr, uint32_t wrap, uint64_t read_addr, bool linked, uint32_t transaction_id) {

  uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
  volatile uint32_t* ptr = (volatile uint32_t*)offset;
  uint32_t atomic_resp = NOC_STATUS_READ_REG(noc, NIU_MST_ATOMIC_RESP_RECEIVED);

  ptr[NOC_TARG_ADDR_LO >> 2] = (uint32_t)(addr & 0xFFFFFFFF);
  ptr[NOC_TARG_ADDR_MID >> 2] = addr >> 32;
  // ptr[NOC_PACKET_TAG_OFFSET >> 2] = NOC_PACKET_TAG_TRANSACTION_ID(transaction_id);
  ptr[NOC_RET_ADDR_LO >> 2] = (uint32_t)(read_addr & 0xFFFFFFFF);
  ptr[NOC_RET_ADDR_MID >> 2] = (uint32_t)(read_addr >> 32);
  ptr[NOC_CTRL >> 2] = (linked ? NOC_CMD_VC_LINKED : 0x0) |
                       NOC_CMD_AT |
                       NOC_CMD_RESP_MARKED;
  ptr[NOC_AT_LEN_BE >> 2] = NOC_AT_INS(NOC_AT_INS_INCR_GET) | NOC_AT_WRAP(wrap) | NOC_AT_IND_32((addr>>2) & 0x3) | NOC_AT_IND_32_SRC(0);
  ptr[NOC_AT_DATA >> 2] = incr;
  ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;

  atomic_resp++;
  while (atomic_resp != NOC_STATUS_READ_REG(noc, NIU_MST_ATOMIC_RESP_RECEIVED));
}

/*
inline void noc_fast_atomic_increment(uint32_t noc, uint32_t cmd_buf, uint64_t addr, uint32_t incr, uint32_t wrap, bool linked) {
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_TARG_ADDR_LO, (uint32_t)(addr & 0xFFFFFFFF));
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_TARG_ADDR_MID, (uint32_t)(addr >> 32));
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_CTRL, (linked ? NOC_CMD_VC_LINKED : 0x0) | NOC_CMD_AT);
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_AT_LEN_BE, NOC_AT_INS(NOC_AT_INS_INCR_GET) | NOC_AT_WRAP(wrap) | NOC_AT_IND_32((addr>>2) & 0x3) | NOC_AT_IND_32_SRC(0));
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_AT_DATA, incr);
  NOC_CMD_BUF_WRITE_REG(noc, cmd_buf, NOC_CMD_CTRL, 0x1);
}

inline void noc_fast_atomic_increment_l1(uint32_t noc, uint32_t cmd_buf, uint64_t addr, uint32_t incr, uint32_t wrap, bool linked) {
  NOC_CMD_BUF_WRITE_REG_L1(noc, cmd_buf, NOC_TARG_ADDR_LO, (uint32_t)(addr & 0xFFFFFFFF));
  NOC_CMD_BUF_WRITE_REG_L1(noc, cmd_buf, NOC_TARG_ADDR_MID, (uint32_t)(addr >> 32));
  NOC_CMD_BUF_WRITE_REG_L1(noc, cmd_buf, NOC_CTRL, (linked ? NOC_CMD_VC_LINKED : 0x0) | NOC_CMD_AT);
  NOC_CMD_BUF_WRITE_REG_L1(noc, cmd_buf, NOC_AT_LEN_BE, NOC_AT_INS(NOC_AT_INS_INCR_GET) | NOC_AT_WRAP(wrap) | NOC_AT_IND_32((addr>>2) & 0x3) | NOC_AT_IND_32_SRC(0));
  NOC_CMD_BUF_WRITE_REG_L1(noc, cmd_buf, NOC_AT_DATA, incr);
  NOC_CMD_BUF_WRITE_REG_L1(noc, cmd_buf, NOC_CMD_CTRL, 0x1);
}
*/
