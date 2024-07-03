// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "noc_nonblocking_api.h"

void ncrisc_noc_fast_read_l1(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  while (NOC_STATUS_READ_REG_L1(noc, NIU_MST_REQS_OUTSTANDING_ID(transaction_id)) > ((NOC_MAX_TRANSACTION_ID_COUNT+1)/2));

  if (len_bytes > 0) {
    //word offset noc cmd interface
    uint32_t noc_rd_cmd_field = NOC_CMD_CPY | NOC_CMD_RD | NOC_CMD_RESP_MARKED | NOC_CMD_VC_STATIC | NOC_CMD_STATIC_VC(1);
    uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
    volatile uint32_t* ptr = (volatile uint32_t*)offset;

    ptr[NOC_RET_ADDR_LO  >> 2] = dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = 0x0;
    ptr[NOC_RET_ADDR_HI  >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_CTRL >> 2] = noc_rd_cmd_field;
    ptr[NOC_TARG_ADDR_LO  >> 2] = (uint32_t)src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = (uint32_t)(src_addr >> 32) & 0x1000000F;
    ptr[NOC_TARG_ADDR_HI  >> 2] = (uint32_t)(src_addr >> 36) & 0xFFFFFF;
    ptr[NOC_PACKET_TAG >> 2] = NOC_PACKET_TAG_TRANSACTION_ID(transaction_id);
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;
  }
}

void ncrisc_noc_fast_write_l1(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, bool mcast, bool linked, uint32_t num_dests, uint32_t transaction_id) {
  while (NOC_STATUS_READ_REG_L1(noc, NIU_MST_REQS_OUTSTANDING_ID(transaction_id)) > ((NOC_MAX_TRANSACTION_ID_COUNT+1)/2));
  
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
    ptr[NOC_TARG_ADDR_LO  >> 2] = src_addr;
    ptr[NOC_TARG_ADDR_MID >> 2] = 0x0;
    ptr[NOC_TARG_ADDR_HI  >> 2] = noc_xy_local_addr[noc];
    ptr[NOC_RET_ADDR_LO  >> 2] = (uint32_t)dest_addr;
    ptr[NOC_RET_ADDR_MID >> 2] = (uint32_t)(dest_addr >> 32) & 0x1000000F;
    ptr[NOC_RET_ADDR_HI  >> 2] = (uint32_t)(dest_addr >> 36) & 0xFFFFFF;
    ptr[NOC_PACKET_TAG >> 2] = NOC_PACKET_TAG_TRANSACTION_ID(transaction_id);
    ptr[NOC_AT_LEN_BE >> 2] = len_bytes;
    ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;
  }
}

#ifdef RISC_B0_HW
void ncrisc_noc_fast_read_any_len_l1(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
  while (!ncrisc_noc_fast_read_ok_l1(noc, cmd_buf));
  ncrisc_noc_fast_read_l1(noc, cmd_buf, src_addr, dest_addr, len_bytes, transaction_id); 
}
#else
void ncrisc_noc_fast_read_any_len_l1(uint32_t noc, uint32_t cmd_buf, uint64_t src_addr, uint32_t dest_addr, uint32_t len_bytes, uint32_t transaction_id) {
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
#endif

void ncrisc_noc_fast_write_any_len_l1(uint32_t noc, uint32_t cmd_buf, uint32_t src_addr, uint64_t dest_addr, uint32_t len_bytes, uint32_t vc, bool mcast, bool linked, uint32_t num_dests, uint32_t transaction_id) {
  while (len_bytes > NOC_MAX_BURST_SIZE) {
    while (!ncrisc_noc_fast_write_ok_l1(noc, cmd_buf));
    ncrisc_noc_fast_write_l1(noc, cmd_buf, src_addr, dest_addr, NOC_MAX_BURST_SIZE, vc, mcast, linked, num_dests, transaction_id);
    src_addr += NOC_MAX_BURST_SIZE;
    dest_addr += NOC_MAX_BURST_SIZE;
    len_bytes -= NOC_MAX_BURST_SIZE;
    if (!ncrisc_noc_fast_write_ok_l1(noc, cmd_buf)) {
      cmd_buf++;
      if (cmd_buf >= NCRISC_SMALL_TXN_CMD_BUF) cmd_buf = NCRISC_WR_CMD_BUF;
    }
  }
  while (!ncrisc_noc_fast_write_ok_l1(noc, cmd_buf));
  ncrisc_noc_fast_write_l1(noc, cmd_buf, src_addr, dest_addr, len_bytes, vc, mcast, linked, num_dests, transaction_id);
}

void noc_atomic_read_and_increment_l1(uint32_t noc, uint32_t cmd_buf, uint64_t addr, uint32_t incr, uint32_t wrap, uint64_t read_addr, bool linked, uint32_t transaction_id) {

  while (NOC_STATUS_READ_REG_L1(noc, NIU_MST_REQS_OUTSTANDING_ID(transaction_id)) > ((NOC_MAX_TRANSACTION_ID_COUNT+1)/2));

  uint32_t offset = (cmd_buf << NOC_CMD_BUF_OFFSET_BIT) + (noc << NOC_INSTANCE_OFFSET_BIT);
  volatile uint32_t* ptr = (volatile uint32_t*)offset;
  uint32_t atomic_resp = NOC_STATUS_READ_REG(noc, NIU_MST_ATOMIC_RESP_RECEIVED);

  ptr[NOC_TARG_ADDR_LO  >> 2] = (uint32_t)(addr & 0xFFFFFFFF);
  ptr[NOC_TARG_ADDR_MID >> 2] = (uint32_t)(addr >> 32) & 0x1000000F;
  ptr[NOC_TARG_ADDR_HI  >> 2] = (uint32_t)(addr >> 36) & 0xFFFFFF;
  ptr[NOC_PACKET_TAG >> 2] = NOC_PACKET_TAG_TRANSACTION_ID(transaction_id);
  ptr[NOC_RET_ADDR_LO  >> 2] = (uint32_t)(read_addr & 0xFFFFFFFF);
  ptr[NOC_RET_ADDR_MID >> 2] = (uint32_t)(read_addr >> 32) & 0x1000000F;
  ptr[NOC_RET_ADDR_HI  >> 2] = (uint32_t)(read_addr >> 36) & 0xFFFFFF;
  ptr[NOC_CTRL >> 2] = (linked ? NOC_CMD_VC_LINKED : 0x0) |
                       NOC_CMD_AT |
                       NOC_CMD_RESP_MARKED;
  ptr[NOC_AT_LEN_BE >> 2] = NOC_AT_INS(NOC_AT_INS_INCR_GET) | NOC_AT_WRAP(wrap) | NOC_AT_IND_32((addr>>2) & 0x3) | NOC_AT_IND_32_SRC(0);
  ptr[NOC_AT_DATA >> 2] = incr;
  ptr[NOC_CMD_CTRL >> 2] = NOC_CTRL_SEND_REQ;

  atomic_resp++;
  while (atomic_resp != NOC_STATUS_READ_REG(noc, NIU_MST_ATOMIC_RESP_RECEIVED));
}
