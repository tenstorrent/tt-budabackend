// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "eth_ss.h"
#include "eth_init.h"

uint64_t eth_mac_get_num_crc_err() {
  uint64_t result = eth_mac_reg_read(0x92C); // Rx_CRC_Error_Frames_High
  result = result >> 32;
  result += eth_mac_reg_read(0x928); // Rx_CRC_Error_Frames_Low
  return result;
}


void eth_send_raw(uint32_t q_num, uint32_t src_addr, uint32_t num_bytes, uint64_t dest_mac_addr) {
  while (eth_txq_reg_read(q_num, ETH_TXQ_CMD) != 0) {
    LOGT("%s", "polling ETH_TXQ_CMD to finish...\n");
  }
  eth_txq_reg_write(q_num, ETH_TXQ_DEST_MAC_ADDR_HI, dest_mac_addr >> 32);
  eth_txq_reg_write(q_num, ETH_TXQ_DEST_MAC_ADDR_LO, dest_mac_addr & 0xFFFFFFFF);
  eth_txq_reg_write(q_num, ETH_TXQ_TRANSFER_START_ADDR, src_addr);
  eth_txq_reg_write(q_num, ETH_TXQ_TRANSFER_SIZE_BYTES, num_bytes);
  eth_txq_reg_write(q_num, ETH_TXQ_CMD, ETH_TXQ_CMD_START_RAW);
}


void eth_send_packet(uint32_t q_num, uint32_t src_word_addr, uint32_t dest_word_addr, uint32_t num_words) {
  volatile uint32_t *post_code = (volatile uint32_t *) 0xffb3010c;
  uint16_t count = 0;
  while (eth_txq_reg_read(q_num, ETH_TXQ_CMD) != 0) {
    *post_code = 0xDED10000 | count;
    count++;
    LOGT("%s", "polling ETH_TXQ_CMD to finish...\n");
  }
  eth_txq_reg_write(q_num, ETH_TXQ_TRANSFER_START_ADDR, src_word_addr << 4);
  eth_txq_reg_write(q_num, ETH_TXQ_DEST_ADDR, dest_word_addr << 4);
  eth_txq_reg_write(q_num, ETH_TXQ_TRANSFER_SIZE_BYTES, num_words << 4);
  eth_txq_reg_write(q_num, ETH_TXQ_CMD, ETH_TXQ_CMD_START_DATA);
}

void eth_ecc_en(uint32_t irqs_en, uint32_t paritys_en)
{
  eth_ctrl_reg_write(ETH_CTRL_ECC_PAR_EN, paritys_en);

  // Delay a few cycles to allow change to propagate
  eth_ctrl_reg_read(ETH_CTRL_ECC_STATUS);
  eth_ctrl_reg_read(ETH_CTRL_ECC_STATUS);
  eth_ctrl_reg_read(ETH_CTRL_ECC_STATUS);
  eth_ctrl_reg_read(ETH_CTRL_ECC_STATUS);

  eth_ctrl_reg_write(ETH_CTRL_ECC_INT_EN, irqs_en);
}

void eth_ecc_clr(uint32_t irqs_to_clr)
{
  eth_ctrl_reg_write(ETH_CTRL_ECC_CLR, irqs_to_clr);
}

void eth_ecc_force(uint32_t irqs_to_force)
{
  eth_ctrl_reg_write(ETH_CTRL_ECC_FORCE, irqs_to_force);
}

uint32_t eth_ecc_get_status()
{
  return eth_ctrl_reg_read(ETH_CTRL_ECC_STATUS);
}

uint32_t eth_ecc_get_count(uint32_t source_idx)
{
  return eth_ctrl_reg_read(ETH_CTRL_ECC_CNT + (source_idx<<2));
}

bool eth_no_ecc_errors_detected()
{
  bool detected = false;

  for (int k = 0; k < NUM_ECC_SOURCES; k++) {
    detected = detected || (eth_ecc_get_count(k) > 0);
  }

  return !detected;
}
