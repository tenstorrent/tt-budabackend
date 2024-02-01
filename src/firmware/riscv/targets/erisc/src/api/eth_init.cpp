// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>

#include "eth_init.h"
#include "noc.h"
#include "eth_ss.h"
#include "eth_routing_v2.h"

#define STATIC_RX_LNKUP_TIMEOUT_INITIAL 30000  // longer initial timeout to allow shelves to power-up
#define STATIC_RX_LNKUP_TIMEOUT_RESTART 5000   // timeout for retrain scenario
#define DUMMY_PACKET_TIMEOUT 30000             // dummpy packet timeout

node_info_t* const my_node_info = (node_info_t*)(NODE_INFO_ADDR);

static uint64_t last_timestamp;
static uint32_t curr_an_attempts;
static uint32_t an_attempts;
static uint32_t pg_rcv_fail;
static bool no_an_initial_attempt = 0;

static uint32_t next_lane_tx_eq_state[4];
static uint32_t next_lane_rx_eq_state[4];
static uint32_t curr_lane_tx_eq_state[4];
static uint32_t curr_lane_rx_eq_state[4];

static uint32_t tx_eq_lp_ceu[4];
static uint32_t tx_eq_ld_sts[4];
static uint32_t tx_eq_phy_update_req[4];
// static uint32_t rx_eq_ongoing_update_req[4];
static uint32_t rx_eq_ld_ceu[4];
static uint32_t rx_eq_lp_sts[4];

static uint32_t lane_training_frame_locked_iters[4];
static uint32_t lane_training_frame_locked_iters_last_1ms[4];
static bool lane_frame_lock[4];

clk_counter_t clk_counter;

// static uint32_t ld_sts[4];
// static uint32_t lp_sts[4];
// static uint32_t ld_ceu[4];
// static uint32_t lp_ceu[4];

// FW training state

// TX EQ FSM
const uint32_t TX_EQ_WAIT_UPDATE_CMD = 0;
const uint32_t TX_EQ_WAIT_PHY_UPDATE_STS = 1;
const uint32_t TX_EQ_WAIT_HOLD_CMD = 2;
const uint32_t TX_EQ_WAIT_PHY_HOLD_STS = 3;
const uint32_t TX_EQ_DONE = 4;

// RX EQ FSM
const uint32_t RX_EQ_ISSUE_INIT = 0;
const uint32_t RX_EQ_ISSUE_ADAPT_REQ = 1;
const uint32_t RX_EQ_WAIT_PHY_UPDATE_CMD = 2;
const uint32_t RX_EQ_WAIT_UPDATE_STS = 3;
const uint32_t RX_EQ_WAIT_HOLD_STS = 4;
const uint32_t RX_EQ_WAIT_CLEAR_ADAPT_ACK = 5;
const uint32_t RX_EQ_ISSUE_FINAL_ADAPT_REQ = 6;
const uint32_t RX_EQ_WAIT_FINAL_PHY_UPDATE_CMD = 7;
const uint32_t RX_EQ_WAIT_FINAL_CLEAR_ADAPT_ACK = 8;
const uint32_t RX_EQ_DONE = 9;

inline void get_timestamp(volatile uint32_t* ptr) {
  uint64_t timestamp = eth_read_wall_clock();
  ptr[0] = timestamp >> 32;
  ptr[1] = timestamp & 0xFFFFFFFF;
  last_timestamp = timestamp;
}

inline void get_time_since_last_timestamp(volatile uint32_t* ptr) {
  uint64_t curr_time = eth_read_wall_clock();
  uint64_t timestamp = curr_time - last_timestamp;
  ptr[0] = timestamp >> 32;
  ptr[1] = timestamp & 0xFFFFFFFF;
}

inline void get_curr_time(volatile uint32_t* ptr) {
  uint64_t timestamp = eth_read_wall_clock();
  ptr[0] = timestamp >> 32;
  ptr[1] = timestamp & 0xFFFFFFFF;
}

////

const uint32_t EVENT_PCS_PHY_POWERUP = 1;
const uint32_t EVENT_LINK_ACTIVE = 2;
const uint32_t EVENT_NO_AN_BRINGUP_START = 3;
const uint32_t EVENT_NO_AN_BRINGUP_DONE = 4;
const uint32_t EVENT_CRC_TEST_START = 5;
const uint32_t EVENT_CRC_TEST_DONE = 6;
const uint32_t EVENT_AN_INT_LINK_RESTART = 7;
const uint32_t EVENT_LINK_BROKEN_RESTART = 8;
const uint32_t EVENT_PCS_RESET = 9;
const uint32_t EVENT_AN_RESTART = 10;
const uint32_t EVENT_AN_INTR_NO_PG_RCV = 11;
const uint32_t EVENT_AN_INTR_PG_RCV = 12;
const uint32_t EVENT_LINK_TRAINING_START = 13;
const uint32_t EVENT_LANE_FRAME_LOCK_LOST = 14;
const uint32_t EVENT_CRC_CHECK_FAILED = 15;
const uint32_t EVENT_LANE_ERR_AN_RESTART = 16;
const uint32_t EVENT_AN_INTR_COMPLETE = 17;
const uint32_t EVENT_AN_INTR_NOT_COMPLETE = 18;
const uint32_t EVENT_PCS_LINK_ON = 19;
const uint32_t EVENT_SYMERR_CHECK_FAILED = 20;
const uint32_t EVENT_SYMERR_CHECK_PASS = 21;
const uint32_t EVENT_LINK_RESTART_CHECK_PASS = 22;
const uint32_t EVENT_LANE_STATUS_UPDATE = 23;
const uint32_t EVENT_ALL_LANES_TRAINED = 24;
const uint32_t EVENT_LINK_TRAINING_DONE_ALL_LANES_OK = 25;
const uint32_t EVENT_LINK_NOT_ACTIVE = 26;
const uint32_t EVENT_LINK_TEST_MODE_TIMEOUT = 27;
const uint32_t EVENT_LINK_TEST_MODE_COMPLETED = 28;
const uint32_t EVENT_LINK_SIGDET_FAILED = 29;
// const uint32_t EVENT_LINK_PWR_OFF = 30;


void log_link_bringup_event(volatile boot_params_t* link_cfg, uint32_t event_id, uint32_t num_data, const uint32_t* data) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  debug_buf[100+event_id]++;
  get_curr_time(&(debug_buf[100 + 32 + 2*event_id]));
  if (!link_cfg->link_bringup_log_enable) {
    return;
  }

  uint32_t index = debug_buf[200];
  uint32_t end_index = index + 4 + num_data;

  // 54 DWs for log size, reset the log buf
  uint32_t link_bringup_log_size_dws = 54;
  if (end_index >= link_bringup_log_size_dws) {
    index = 0;
    debug_buf[200] = index;
    for (uint32_t i = 0; i < 55; i ++) {
      debug_buf[201+i] = 0;
    }
    return;
  }
  get_curr_time(&(debug_buf[201+index]));
  debug_buf[201+index+2] = event_id;
  debug_buf[201+index+3] = num_data;
  for (uint32_t i = 0; i < num_data; i++) {
    debug_buf[201+index+4+i] = data[i];
  }

  index += (num_data + 4);
  debug_buf[200] = index;
}


void log_link_bringup_event(volatile boot_params_t* link_cfg, uint32_t event_id, uint32_t val) {
  log_link_bringup_event(link_cfg, event_id, 1, &val);
}


void log_link_bringup_event(volatile boot_params_t* link_cfg, uint32_t event_id) {
  log_link_bringup_event(link_cfg, event_id, 0, NULL);
}


uint64_t eth_read_wall_clock() {
  uint32_t wall_clock_low = eth_risc_reg_read(ETH_RISC_WALL_CLOCK_0);
  uint32_t wall_clock_high = eth_risc_reg_read(ETH_RISC_WALL_CLOCK_1_AT);
  return (((uint64_t)wall_clock_high) << 32) | wall_clock_low;
}


void eth_wait_cycles(uint32_t wait_cycles) {
  if (wait_cycles == 0) {
    return;
  }
  uint64_t curr_timer = eth_read_wall_clock();
  uint64_t end_timer = curr_timer + wait_cycles;
  while (curr_timer < end_timer) {
    curr_timer = eth_read_wall_clock();
  }
}

void refclk_counter_init() {
  volatile boot_params_t* link_cfg = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
  if (link_cfg->aiclk_ps == 0) {
    clk_counter.aiclk_scalar = 45; // For 1200MHz
  } else {
    clk_counter.aiclk_scalar = (BOOT_REFCLK_PERIOD_PS / link_cfg->aiclk_ps);
  }
  clk_counter.refclk_scalar = clk_counter.aiclk_scalar;
}

uint64_t eth_read_refclk() {
  // Do a high, low, high read just in case the read is done while the refclk is changing
  uint32_t refclk_hi_init, refclk_hi, refclk_lo;

  refclk_hi_init = ETH_READ_REG(ARC_REFCLK_ADDR_HI);
  refclk_lo = ETH_READ_REG(ARC_REFCLK_ADDR_LO);
  refclk_hi = ETH_READ_REG(ARC_REFCLK_ADDR_HI);

  if (refclk_hi_init != refclk_hi) {
    refclk_lo = ETH_READ_REG(ARC_REFCLK_ADDR_LO);
  }

  return (((uint64_t)refclk_hi) << 32) | refclk_lo;
}

uint64_t eth_refclk_timestamp_init(uint32_t interval) {
  uint64_t curr_timestamp = eth_read_refclk();

  if (curr_timestamp != 0) {
    if (clk_counter.refclk_timeout) {
      if (curr_timestamp != clk_counter.prev_refclk_count) {
        clk_counter.refclk_timeout = false;
        clk_counter.refclk_valid = true;
        clk_counter.refclk_scalar = 1;
      }
    } else {
      clk_counter.refclk_valid = true;
      clk_counter.refclk_scalar = 1;
      clk_counter.refclk_invalid_counter = 0;
    }
  }
  clk_counter.prev_refclk_count = curr_timestamp;

  if (!clk_counter.refclk_valid) {
    curr_timestamp = eth_read_wall_clock();
  }

  return curr_timestamp + (interval * clk_counter.refclk_scalar);
}

bool eth_refclk_timestamp_elapsed(uint64_t *current_timestamp, uint64_t *target_timestamp) {
  bool timeout = false;
  uint64_t refclk_count = eth_read_refclk();

  if (clk_counter.refclk_valid) {
    if (refclk_count != clk_counter.prev_refclk_count) {
      clk_counter.refclk_invalid_counter = 0;
    } else {
      clk_counter.refclk_invalid_counter++;
      // Check if the counter is changing for 3ms interval
      if (clk_counter.refclk_invalid_counter > (27000 * 3 * clk_counter.aiclk_scalar)) {
        clk_counter.refclk_timeout = true;
        clk_counter.refclk_valid = false;
        clk_counter.refclk_scalar = clk_counter.aiclk_scalar;
        timeout = true;
      }
    }
    *current_timestamp = refclk_count;
  } else {
    *current_timestamp = eth_read_wall_clock();
  }

  clk_counter.prev_refclk_count = refclk_count;

  if (timeout) {
    *current_timestamp = eth_read_wall_clock();
    return true;
  } else {
    return (*current_timestamp > *target_timestamp);
  }
}

void eth_reset_deassert() {
  eth_ctrl_reg_write(ETH_CTRL_ETH_RESET, ETH_SOFT_RESETN | ETH_MAC_SOFT_PWR_ON_RESETN | ETH_PCS_SOFT_PWR_ON_RESETN | ETH_MAC_SOFT_RESETN | ETH_PHY_SOFT_RESETN);
}

void eth_reset_assert() {
  eth_ctrl_reg_write(ETH_CTRL_ETH_RESET, 0x0);
}

void eth_risc_reg_write(uint32_t offset, uint32_t val)  {
  ETH_WRITE_REG(ETH_RISC_REGS_START+offset, val);
}

uint32_t eth_risc_reg_read(uint32_t offset) {
  return ETH_READ_REG(ETH_RISC_REGS_START+offset);
}

void eth_mac_reg_write(uint32_t offset, uint32_t val)  {
  ETH_WRITE_REG(ETH_MAC_REGS_START+offset, val);
}

uint32_t eth_mac_reg_read(uint32_t offset) {
  return ETH_READ_REG(ETH_MAC_REGS_START+offset);
}

void eth_pcs_dir_reg_write(uint8_t reg_id, uint16_t val) {
  ETH_WRITE_REG(ETH_PCS_REGS_START+(((uint32_t)reg_id) << 2), val);
}

uint16_t eth_pcs_dir_reg_read(uint8_t reg_id) {
  return (uint16_t)(ETH_READ_REG(ETH_PCS_REGS_START+(((uint32_t)reg_id) << 2)));
}

void eth_pcs_ind_reg_write(uint32_t offset, uint16_t val) {
  eth_pcs_dir_reg_write(ETH_PCS_IND_REG, offset >> 8);
  eth_pcs_dir_reg_read(ETH_PCS_IND_REG);
  eth_pcs_dir_reg_write(offset & 0xFF, val);
}

uint16_t eth_pcs_ind_reg_read(uint32_t offset) {
  eth_pcs_dir_reg_write(ETH_PCS_IND_REG, offset >> 8);
  eth_pcs_dir_reg_read(ETH_PCS_IND_REG);
  return eth_pcs_dir_reg_read(offset & 0xFF);
}

void eth_pcs_ind_reg_rmw(uint32_t offset, uint16_t val, uint32_t lsb, uint32_t num_bits) {
  uint16_t old_val = eth_pcs_ind_reg_read(offset);
  uint16_t mask = ~(0xFFFF << num_bits);
  mask = mask << lsb;
  uint16_t new_val = (old_val & ~mask) | ((val << lsb) & mask);
  ETH_LOG("Modifying PCS reg_id=0x%x, old_val=0x%x, new_val=0x%x\n", offset, old_val, new_val);
  eth_pcs_ind_reg_write(offset, new_val);
}

void eth_pcs_set_cr_para_sel(uint32_t val) {

  uint32_t curr_reg_val = eth_pcs_ind_reg_read(0x18090); // VR_PMA_Gen5_12G_MISC_CTRL0
  uint32_t bit_mask = (0x1 << 14); // CR_PARA_SEL
  curr_reg_val = (curr_reg_val & ~bit_mask) | ((val << 14) & bit_mask);
  eth_pcs_ind_reg_write(0x18090, curr_reg_val);
}

void eth_ctrl_reg_write(uint32_t offset, uint32_t val) {
  ETH_WRITE_REG(ETH_CTRL_REGS_START+offset, val);
}

uint32_t eth_ctrl_reg_read(uint32_t offset) {
  return ETH_READ_REG(ETH_CTRL_REGS_START+offset);
}

void eth_txq_reg_write(uint32_t qnum, uint32_t offset, uint32_t val) {
  ETH_WRITE_REG(ETH_TXQ0_REGS_START+(qnum*ETH_TXQ_REGS_SIZE)+offset, val);
}

uint32_t eth_txq_reg_read(uint32_t qnum, uint32_t offset) {
  return ETH_READ_REG(ETH_TXQ0_REGS_START+(qnum*ETH_TXQ_REGS_SIZE)+offset);
}

void eth_rxq_reg_write(uint32_t qnum, uint32_t offset, uint32_t val) {
  ETH_WRITE_REG(ETH_RXQ0_REGS_START+(qnum*ETH_RXQ_REGS_SIZE)+offset, val);
}

uint32_t eth_rxq_reg_read(uint32_t qnum, uint32_t offset) {
  return ETH_READ_REG(ETH_RXQ0_REGS_START+(qnum*ETH_RXQ_REGS_SIZE)+offset);
}

void eth_pcs_ind_reg_poll(uint32_t reg_id, uint16_t value, uint16_t reg_mask, uint32_t poll_wait, volatile uint32_t* dbg_val_ptr) {
  uint16_t read_val = eth_pcs_ind_reg_read(reg_id) & reg_mask;
  uint16_t read_val_masked = read_val & reg_mask;
  ETH_LOG("Polling PCS reg_id=0x%x, expecting=0x%x, read=0x%x, mask=0x%x, read_masked=0x%x\n", reg_id, value, read_val, reg_mask, read_val_masked);
  while (value != read_val_masked){
    eth_wait_cycles(poll_wait);
    read_val = eth_pcs_dir_reg_read(reg_id & 0xFF);
    read_val_masked = read_val & reg_mask;
    ETH_LOG("Polling PCS reg_id=0x%x, expecting=0x%x, read=0x%x, mask=0x%x, read_masked=0x%x\n", reg_id, value, read_val, reg_mask, read_val_masked);
    if (dbg_val_ptr != 0) {
      dbg_val_ptr[0] = read_val;
    }
  }
  ETH_LOG("Polling PCS reg_id=0x%x, got expected=0x%x\n", reg_id, value);
}


// VR_PMA_Gen5_CR_CTRL
// Size: 16 bits
// Offset: 0x180a0

// VR_PMA_Gen5_CR_ADDR
// Size: 16 bits
// Offset: 0x180a1

// VR_PMA_Gen5_CR_DATA
// Size: 16 bits
// Offset: 0x180a2

void eth_phy_cr_write(uint16_t cr_addr, uint16_t cr_data) {
// 1. Read the VR_PMA_Gen5_CR_CTRL and wait till Bit 0 of this register reads as 0.
  uint32_t val;
  do {
    val = eth_pcs_ind_reg_read(0x180a0);
  } while ((val & 0x1) != 0);
// 2. Write the Synopsys PHY register address to VR_PMA_Gen5_CR_ADDR.
  eth_pcs_ind_reg_write(0x180a1, cr_addr);
// 3. Write the data to be written to the VR_PMA_Gen5_CR_DATA.
  eth_pcs_ind_reg_write(0x180a2, cr_data);
// 4. Write 1 to Bit 1 of the VR_PMA_Gen5_CR_CTRL.
// 5. Write 1 to Bit 0 of the VR_PMA_Gen5_CR_CTRL.
  eth_pcs_ind_reg_write(0x180a0, 0x3);
// 6. Read the VR_PMA_Gen5_CR_CTRL and wait till Bit 0 of this register reads as 0.
  do {
    val = eth_pcs_ind_reg_read(0x180a0);
  } while ((val & 0x1) != 0);
}


uint16_t eth_phy_cr_read(uint16_t cr_addr) {
// 1. Read the VR_PMA_Gen5_CR_CTRL and wait till Bit 0 of this register reads as 0.
  uint32_t val;
  do {
    val = eth_pcs_ind_reg_read(0x180a0);
  } while ((val & 0x1) != 0);
// 2. Write the Synopsys PHY register address to VR_PMA_Gen5_CR_ADDR.
  eth_pcs_ind_reg_write(0x180a1, cr_addr);
// 3. Write 0 to Bit 1 of the VR_PMA_Gen5_CR_CTRL.
// 4. Write 1 to Bit 0 of the VR_PMA_Gen5_CR_CTRL.
  eth_pcs_ind_reg_write(0x180a0, 0x1);
// 5. Read the VR_PMA_Gen5_CR_CTRL and wait till Bit 0 of this register reads as 0.
  do {
    val = eth_pcs_ind_reg_read(0x180a0);
  } while ((val & 0x1) != 0);
// 6. Read the VR_PMA_Gen5_CR_DATA. The register read in this step contains the Read Data
  val = eth_pcs_ind_reg_read(0x180a2);
  return val;
}


void eth_mac_init(volatile boot_params_t* link_cfg) {

  // MTL_Operation_Mode - set WSP arbitration for RX
  eth_mac_reg_write(0x1000, (0x1 << 2));

  // MTL_TxQ(#i)_Operation_Mode Offset: (0x0080*i)+0x1100
  // all cases where we send packet/stream data, rather than just dumping mac memory on crc error
  // set enable + 4KB queue size + traffic class 0 for both queues + TSF if indicated
  eth_mac_reg_write(0x1100, 0xf0008 | (0x0 << 8) | (link_cfg->tx_store_and_forward << 1));
  eth_mac_reg_write(0x1180, 0xf0008 | (0x0 << 8) | (link_cfg->tx_store_and_forward << 1));

  if (link_cfg->tx_store_and_forward) {
    eth_txq_reg_write(0, ETH_TXQ_BURST_LEN, link_cfg->burst_len);
    eth_txq_reg_write(1, ETH_TXQ_BURST_LEN, link_cfg->burst_len);
  } else {
    eth_txq_reg_write(0, ETH_TXQ_BURST_LEN, 0);
    eth_txq_reg_write(1, ETH_TXQ_BURST_LEN, 0);
  }

  // MTL_RxQ(#i)_Operation_Mode Offset: (0x0080*i)+0x1140
  // setup when we send packets/stream data
  //  set FUF + RSF + 8KB queue size
  eth_mac_reg_write(0x1140, 0x1F0028);
  eth_mac_reg_write(0x11C0, 0x1F0028);
  // RX Q en
  eth_mac_reg_write(0x140, 0xA);

  eth_mac_reg_write(0x300, link_cfg->local_mac_addr_high);  // MAC_Address0_High
  eth_mac_reg_write(0x304, link_cfg->local_mac_addr_low);  // MAC_Address0_Low

  // also set mac addr in tx queue control logic
  eth_txq_reg_write(0, ETH_TXQ_SRC_MAC_ADDR_HI, link_cfg->local_mac_addr_high);
  eth_txq_reg_write(0, ETH_TXQ_SRC_MAC_ADDR_LO, link_cfg->local_mac_addr_low);
  eth_txq_reg_write(1, ETH_TXQ_SRC_MAC_ADDR_HI, link_cfg->local_mac_addr_high);
  eth_txq_reg_write(1, ETH_TXQ_SRC_MAC_ADDR_LO, link_cfg->local_mac_addr_low);

  // MAC_Packet_Filter
  // set promiscuous mode for now
  eth_mac_reg_write(0x8, 0x1);

  eth_mac_reg_write(0x150, (0x1 << 15) | (0x0 << 8) | (0x1 << 0)); // MAC_RxQ_Mapping_Ctrl0

  if (link_cfg->loopback_mode == LOOPBACK_MAC) { // MAC loopback
    eth_mac_reg_write(0x0, 0x30000001); // tx enable
    eth_mac_reg_write(0x4, (0x1 << 10) | 0x7); // rx enable + drop crc/pad + loopback
  } else {
    eth_mac_reg_write(0x0, 0x30000001); // tx enable
    eth_mac_reg_write(0x4, 0x7); // rx enable + drop crc/pad
  }
}


void eth_rxq_init(uint32_t q_num, uint32_t rec_buf_word_addr, uint32_t rec_buf_size_words, bool rec_buf_wrap,  bool packet_mode) {
  eth_rxq_reg_write(q_num, ETH_RXQ_BUF_START_WORD_ADDR, rec_buf_word_addr);
  eth_rxq_reg_write(q_num, ETH_RXQ_BUF_PTR, 0);
  eth_rxq_reg_write(q_num, ETH_RXQ_BUF_SIZE_WORDS, rec_buf_size_words);
  uint32_t ctrl_val = eth_rxq_reg_read(q_num, ETH_RXQ_CTRL);
  if (packet_mode) {
    ctrl_val |= ETH_RXQ_CTRL_PACKET_MODE;
  } else {
    ctrl_val &= (~ETH_RXQ_CTRL_PACKET_MODE);
  }
  if (rec_buf_wrap) {
    ctrl_val |= ETH_RXQ_CTRL_BUF_WRAP;
  } else {
    ctrl_val &= (~ETH_RXQ_CTRL_BUF_WRAP);
  }
  eth_rxq_reg_write(q_num, ETH_RXQ_CTRL, ctrl_val);
}


void eth_write_remote_reg(uint32_t q_num, uint32_t reg_addr, uint32_t val) {
  while (eth_txq_reg_read(q_num, ETH_TXQ_CMD) != 0) {
    LOGT("%s", "polling ETH_TXQ_CMD to finish...\n");
  }
  eth_txq_reg_write(q_num, ETH_TXQ_DEST_ADDR, reg_addr);
  eth_txq_reg_write(q_num, ETH_TXQ_REMOTE_REG_DATA, val);
  eth_txq_reg_write(q_num, ETH_TXQ_CMD, ETH_TXQ_CMD_START_REG);
}


void vendor_specific_reset_pcs(uint32_t wait_cycles_scalar, volatile boot_params_t* link_cfg) {
  uint32_t sram_init_done_wait_cycles = 8 * wait_cycles_scalar; // ~300ns
  // 9. Initiate the Vendor specific software reset by writing 1’b1 to VR_RST bit [15] of VR_PCS_DIG_CTRL1. [0x38000]
  eth_pcs_ind_reg_rmw(0x38000, 0x1, 15, 1);
  // 10. If SRAM support is enabled in the PHY, perform steps mentioned in “Programming steps for SRAM Interface of PHY”.
  //     11.2 Programming steps for SRAM Interface of PHY
  eth_pcs_ind_reg_poll(0x1809a, 0x1, 0x1);       // Poll bit [0] (INIT_DN) of VR_PMA_SNPS_MP_25G_16G_SRAM (0x1809A) till it becomes 1.

  // After waiting for a duration of at least 10 clock cycles of cr_para_clk,
  // software can optionally write to CR registers inside DWC_xlgpcs to modify the
  // contents of SRAM connected to Synopsys MP PHY
  eth_wait_cycles(sram_init_done_wait_cycles);
  // if (link_cfg->ovrd_phy_rom_fw) {
  //   volatile uint16_t *p_phy_fw = (volatile uint16_t*)(link_cfg->phy_rom_fw_16kb_offset * 16 * 1024);
  //   for (uint32_t offset = 0; offset < (link_cfg->phy_fw_size_kb*1024)/2; offset++) {
  //     eth_phy_cr_write(ETH_PHY_SRAM_OVRD_START_ADDR + offset, p_phy_fw[offset]);
  //   }
  // }
  eth_pcs_ind_reg_rmw(0x1809a, 0x1, 1, 1); // Program bit [1] (EXT_LD_DN) of VR_PMA_SNPS_MP_25G_16G_SRAM (0x1809A) to 1.

  // 11. Wait for the bit [15] (VR_RST) of the VR_PCS_DIG_CTRL1 (0x38000) to get cleared.
  eth_pcs_ind_reg_poll(0x38000, 0x0, 0x1 << 15);
}


void override_phy_default_settings(volatile boot_params_t* link_cfg) {
  // MPLLB_LOW_BW: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL3 (0x18078) register to 16’h18.
  eth_pcs_ind_reg_write(0x18078, 0x7);
  // MPLLB_HIGH_BW: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL4 (0x1807A) register to 16’h18.
  eth_pcs_ind_reg_write(0x1807A, 0x7);
  //Tx Misc 1n22
  // eth_phy_cr_write(0x5022, 0xa);
  //apply max vboost on the TX side
  eth_pcs_ind_reg_write(0x18031, 0xf5f0);
// TX0_DCC_CTRL_RNG[3:0], TX1_DCC_CTRL_RNG[7:4], TX2_DCC_CTRL_RNG[11:8], TX3_DCC_CTRL_RNG[15:12]:
  eth_pcs_ind_reg_write(0x18047, 0xbbbb);
  // TX/RX term ctrl 3n22
  eth_phy_cr_write(0x6022, 0xe0);
  // CTLE_BOOST_0[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL0 (0x18058) register to 5’h0C.
  // CTLE_BOOST_1[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL1 (0x18059) register to 5’h0C.
  // CTLE_BOOST_2[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL2 (0x1805A) register to 5’h0C.
  // CTLE_BOOST_3[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL3 (0x1805B) register to 5’h0C.
  // CTLE_POLE_0[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL0 (0x18058) register to 2’h3.
  // CTLE_POLE_1[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL1 (0x18059) register to 2’h3.
  // CTLE_POLE_2[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL2 (0x1805A) register to 2’h3.
  // CTLE_POLE_3[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL3 (0x1805B) register to 2’h3.
  // eth_pcs_ind_reg_rmw(0x18058, 0x664a, 0, 16);
  // eth_pcs_ind_reg_rmw(0x18059, 0x664a, 0, 16);
  // eth_pcs_ind_reg_rmw(0x1805A, 0x664a, 0, 16);
  // eth_pcs_ind_reg_rmw(0x1805B, 0x664a, 0, 16);
  volatile uint32_t* p_ctle = &link_cfg->override_ctle_lane0;
  for (uint32_t i = 0; i < 4; i++) {
    uint8_t ctle_boost = 0;
    uint8_t ctle_pole = 0;
    uint8_t ctle_vga1_gain = 0;
    uint8_t ctle_vga2_gain = 0;
    uint32_t override_ctle = p_ctle[i];
    if (override_ctle != 0) {
      ctle_boost = (override_ctle >> CTLE_BOOST_SHIFT) & CTLE_BOOST_MASK;
      ctle_pole = (override_ctle >> CTLE_POLE_SHIFT) & CTLE_POLE_MASK;
      ctle_vga1_gain = (override_ctle >> CTLE_VGA1_GAIN_SHIFT) & CTLE_VGA1_GAIN_MASK;
      ctle_vga2_gain = (override_ctle >> CTLE_VGA2_GAIN_SHIFT) & CTLE_VGA2_GAIN_MASK;
      eth_pcs_ind_reg_rmw((0x18058 + i), (ctle_boost | (ctle_pole << 5) | (ctle_vga1_gain << 8) | (ctle_vga2_gain << 12)), 0, 16);
    } else {
      eth_pcs_ind_reg_rmw((0x18058 + i), 0x664a, 0, 16);
    }
  }

  // DFE_TAP1_0[7:0], DFE_TAP1_1[15:8]: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_DFE_TAP_CTRL0 (0x1805E) register to 8’hf.
  // DFE_TAP1_2[7:0], DFE_TAP1_3[15:8]: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_DFE_TAP_CTRL1 (0x1805F) register to 8’hf.
  eth_pcs_ind_reg_write(0x1805E, 0x0a0a);
  eth_pcs_ind_reg_write(0x1805F, 0x0a0a);

  // EQ_AFE_RT_0[2:0], EQ_AFE_RT_1[6:4], EQ_AFE_RT_2[10:8], EQ_AFE_RT_3[14:12]:
  eth_pcs_ind_reg_write(0x1805d, 0x1111);
  // RX EQ AFE Rate 3n0d
  // eth_phy_cr_write(0x600d, 0x600);

  // CDR VCO Config
  eth_phy_cr_write(0x6007, 0x1200);

  // Sigdet LF threshold
  eth_phy_cr_write(0x500c, 0x9);

  volatile uint32_t* p_tx_eq = &link_cfg->override_tx_eq_lane0;
  for (uint32_t i = 0; i < 4; i++) {
    uint8_t tx_eq_main = 0;
    uint8_t tx_eq_pre = 0;
    uint8_t tx_eq_post = 0;
    uint32_t override_tx_eq = p_tx_eq[i];
    if (override_tx_eq != 0) {
      tx_eq_main = (override_tx_eq >> TX_EQ_MAIN_SHIFT) & TX_EQ_MASK;
      tx_eq_pre  = (override_tx_eq >> TX_EQ_PRE_SHIFT) & TX_EQ_MASK;
      tx_eq_post = (override_tx_eq >> TX_EQ_POST_SHIFT) & TX_EQ_MASK;
      // TX_EQ_PRE[5:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL0_LN0/1/2/3 (0x18036/0x18038/0x1803A/0x1803C) register to 6’h0.
      // TX_EQ_MAIN[13:8]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL0_LN0/1/2/3 (0x18036/0x18038/0x1803A/0x1803C) register to 6’h18.
      eth_pcs_ind_reg_write((0x18036 + (i * 2)), (((tx_eq_main) << 8) | tx_eq_pre));

      // eth_pcs_ind_reg_write(0x18036, 0x1009);
      // eth_pcs_ind_reg_write(0x18038, 0x1009);
      // eth_pcs_ind_reg_write(0x1803A, 0x1009);
      // eth_pcs_ind_reg_write(0x1803C, 0x1009);

      // TX_EQ_POST[5:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL1_LN0/1/2/3 (0x18037/0x18039/0x1803B/0x1803D) register to 6’h0.
      // TX_EQ_OVER_RIDE[6]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL1_LN0/1/2/3 (0x18037/0x18039/0x1803B/0x1803D) register to 1 when “DWCXLP_LNK_TRAIN_EN” parameter is set.
      eth_pcs_ind_reg_write((0x18037 + (i * 2)), ((0x1 << 6) | tx_eq_post));

      // eth_pcs_ind_reg_write(0x18037, 0x42);
      // eth_pcs_ind_reg_write(0x18039, 0x42);
      // eth_pcs_ind_reg_write(0x1803B, 0x42);
      // eth_pcs_ind_reg_write(0x1803D, 0x42);
    }
  }
}


void enable_packet_mode(volatile boot_params_t* link_cfg) {
  // tx/rx q 0 - enable packet mode
  eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_HI, link_cfg->bcast_mac_addr_high);
  eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_LO, link_cfg->bcast_mac_addr_low);
  eth_txq_reg_write(0, ETH_TXQ_MAX_PKT_SIZE_BYTES, link_cfg->max_packet_size_bytes);
  eth_txq_reg_write(0, ETH_TXQ_REMOTE_SEQ_TIMEOUT, link_cfg->remote_seq_timeout);
  eth_txq_reg_write(0, ETH_TXQ_BURST_LEN, link_cfg->burst_len);
  eth_txq_reg_write(0, ETH_TXQ_MIN_PACKET_SIZE_WORDS, link_cfg->min_packet_size_words);
  eth_txq_reg_write(0, ETH_TXQ_LOCAL_SEQ_UPDATE_TIMEOUT, link_cfg->local_seq_update_timeout);

  eth_txq_reg_write(0, ETH_TXQ_CTRL, (0x1 << 3) | (0x1 << 0)); // enable packet resend, disable drop notifications (i.e. timeout-only resend mode) to avoid #2943 issues

  eth_wait_cycles(1000000);
}


void send_chip_info(volatile boot_params_t* link_cfg) {
  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;

  test_results[64] = link_cfg->board_type;
  test_results[65] = link_cfg->board_id;
  test_results[66] = link_cfg->local_chip_coord;
  test_results[67] = link_cfg->shelf_rack_coord;
  test_results[68] = my_node_info->my_eth_id;
  test_results[69] = 0;
  test_results[70] = 0;
  test_results[71] = 1;
  eth_send_packet(0, ((uint32_t)(&test_results[64]))>>4, ((uint32_t)(&test_results[72]))>>4, 2); //send chip/board data to link partner
}


bool is_cable_loopback(volatile boot_params_t* link_cfg) {
  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  // Loopback detection here
  uint64_t  remote_board_id = test_results[72];
  remote_board_id = (remote_board_id << 32) | test_results[73];
  uint8_t   remote_x        = test_results[74] & 0xFF;;
  uint8_t   remote_y        = (test_results[74] >> 8) & 0xFF;
  uint8_t   remote_rack     = test_results[75] & 0xFF;
  uint8_t   remote_shelf    = (test_results[75] >> 8) & 0xFF;

  uint64_t  local_board_id  = link_cfg->board_type;
  local_board_id = (local_board_id << 32) | link_cfg->board_id;
  uint8_t   local_x         = link_cfg->local_chip_coord & 0xFF;;
  uint8_t   local_y         = (link_cfg->local_chip_coord >> 8) & 0xFF;
  uint8_t   local_rack      = link_cfg->shelf_rack_coord & 0xFF;
  uint8_t   local_shelf     = (link_cfg->shelf_rack_coord >> 8) & 0xFF;

  if ((local_board_id == 0) || (remote_board_id == 0)) {
    return false;
  }

  // Same shelf
  if ((remote_rack == local_rack) && (remote_shelf == local_shelf)) {
    // Same Chip
    if ((remote_x == local_x) && (remote_y == local_y)) {
      // Chip Loopback
      if (remote_board_id == local_board_id) {
        debug_buf[95] = 1;
        return true;
      }
    // LP
    } else if ((remote_x != local_x || remote_y != local_y)) {
      // Credo loopback
      if ((remote_board_id != local_board_id) && (((link_cfg->port_enable_force >> my_node_info->my_eth_id) & 0x1) == 1)) {
        debug_buf[95] = 1;
        return true;
      }
    } else {}
  }
  return false;
}


eth_link_state_t validate_chip_config(volatile boot_params_t* link_cfg) {
  if (!link_cfg->validate_chip_config_en) return LINK_ACTIVE;

  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  debug_buf[96] = LINK_INACTIVE_STATUS_NONE;

  uint64_t  remote_board_id = test_results[72];
  remote_board_id = (remote_board_id << 32) | test_results[73];
  uint8_t   remote_x        = test_results[74] & 0xFF;;
  uint8_t   remote_y        = (test_results[74] >> 8) & 0xFF;
  uint8_t   remote_rack     = test_results[75] & 0xFF;
  uint8_t   remote_shelf    = (test_results[75] >> 8) & 0xFF;

  uint64_t  local_board_id  = link_cfg->board_type;
  local_board_id = (local_board_id << 32) | link_cfg->board_id;
  uint8_t   local_x         = link_cfg->local_chip_coord & 0xFF;;
  uint8_t   local_y         = (link_cfg->local_chip_coord >> 8) & 0xFF;
  uint8_t   local_rack      = link_cfg->shelf_rack_coord & 0xFF;
  uint8_t   local_shelf     = (link_cfg->shelf_rack_coord >> 8) & 0xFF;

  uint32_t cartesian_dist = 0;
  if(remote_x > local_x) cartesian_dist += (remote_x-local_x);
  else cartesian_dist += (local_x-remote_x);
  if(remote_y > local_y) cartesian_dist += (remote_y-local_y);
  else cartesian_dist += (local_y-remote_y);

  // a. If same rack/shelf ids and same chip ids
  //   i.   Same board ID, set LINK_TRAIN_LOOPBACK, no routing allowed, but allow app stream and packet test
  //   ii.  Unique board IDs, set LINK_TRAIN_CONFIG_ERROR_CHIP_ID
  // b. If same rack/shelf ids and unique chip ids
  //   i.   Static training/credo port enable is set, set LINK_TRAIN_LOOPBACK
  //   ii.  Chip ids not increment or decrement by 1 on x or y, set LINK_TRAIN_CONFIG_ERROR_CHIP_ID
  // c. If unique rack ids
  //   i.   Nebula boards, set LINK_TRAIN_CONFIG_ERROR_RACK_ID
  // d. If unique shelf ids
  //   i.   Unique chip ids, set LINK_TRAIN_CONFIG_ERROR_SHELF_ID

  // Same shelf
  if ((remote_board_id == 0) || (local_board_id == 0)) {
    debug_buf[96] = LINK_INACTIVE_ERROR_BOARD_ID;
  } else if ((remote_rack == local_rack) && (remote_shelf == local_shelf)) {
    // Same Chip
    if ((remote_x == local_x) && (remote_y == local_y)) {
      if (remote_board_id != local_board_id) {
        debug_buf[96] = LINK_INACTIVE_ERROR_CHIP_ID;
      }
    // LP
    } else if ((remote_x != local_x || remote_y != local_y)) {
      if ((remote_board_id == local_board_id) || (cartesian_dist > 1)) {
        debug_buf[96] = LINK_INACTIVE_ERROR_CHIP_ID;
      }
    } else {}
  } else if (link_cfg->board_type==NEBULA_X1 || link_cfg->board_type==NEBULA_X2_LEFT || link_cfg->board_type==NEBULA_X2_RIGHT) {
    // NB cards are not supposed to connect to a different rack currently (should be shelf only)
    if (remote_rack != local_rack) debug_buf[96] = LINK_INACTIVE_ERROR_RACK_ID;
  }
  // TODO: Define fail state for LINK_TRAIN_CONFIG_ERROR_SHELF_ID

  if (debug_buf[96] == LINK_INACTIVE_STATUS_NONE) {
    return LINK_ACTIVE;
  } else {
    return LINK_NOT_ACTIVE;
  }
}


bool exchange_dummy_packet(volatile boot_params_t* link_cfg) {
  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;

  // Poll for first dummy packet
  test_results[16] = 0xca11ab1e;

  uint64_t curr_time = 0;
  uint64_t next_1ms_time = 0;
  uint32_t post_training_time_ms = 0;
  bool timeout = false;

  eth_send_packet(0, ((uint32_t)(&test_results[16]))>>4, ((uint32_t)(&test_results[20]))>>4, 1); //send round to otherside
  next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
  do {
    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      post_training_time_ms++;
    }
    timeout = (post_training_time_ms > DUMMY_PACKET_TIMEOUT);
  }
  while(test_results[20] != 0xca11ab1e && !timeout);
  test_results[46] = post_training_time_ms;

  return timeout;
}


void eth_epoch_sync(uint32_t epoch_id, bool has_eth_stream_trans, volatile uint32_t &epoch_id_to_send, volatile uint32_t &other_chip_epoch_id, void (*risc_context_switch)()) {
  node_info_t* const my_node_info = (node_info_t*)(NODE_INFO_ADDR);
  volatile eth_conn_info_t* const eth_conn_info = (volatile eth_conn_info_t*)(ETH_CONN_INFO_ADDR);

  if (eth_conn_info[my_node_info->my_eth_id] > ETH_UNCONNECTED)
  {
    epoch_id_to_send = epoch_id;
    eth_send_packet(0, ((uint32_t)(&epoch_id_to_send))>>4, ((uint32_t)(&other_chip_epoch_id))>>4, 1);

    if (has_eth_stream_trans) {
      uint32_t epoch_id_diff;
      do {
        if (epoch_id > other_chip_epoch_id)
          epoch_id_diff = epoch_id - other_chip_epoch_id;
        else
          epoch_id_diff = 0;

        if (epoch_id_diff < EPOCH_MAX_DIFF)
          break;

        epoch_id_to_send = epoch_id;
        eth_send_packet(0, ((uint32_t)(&epoch_id_to_send))>>4, ((uint32_t)(&other_chip_epoch_id))>>4, 1);

        risc_context_switch();
      } while(true);
    }
  }
}


eth_link_state_t link_powerup_handler(volatile boot_params_t* link_cfg, bool link_restart) {
  log_link_bringup_event(link_cfg, EVENT_PCS_PHY_POWERUP, 0);

  eth_ctrl_reg_write(ETH_CTRL_MISC_CTRL, (5 << 1) | 0x1);

  uint32_t my_eth_id = my_node_info->my_eth_id;
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  if (link_cfg->board_type == NEBULA_X1) {
    if ((my_eth_id !=0) && (my_eth_id != 1) && (my_eth_id != 6) && (my_eth_id != 7)
        && (my_eth_id != 14) && (my_eth_id != 15)) {
      debug_buf[96] = LINK_INACTIVE_PORT_NOT_POPULATED;
      return LINK_NOT_ACTIVE;
    }
  } else if (link_cfg->board_type == NEBULA_X2_LEFT) {
    if ((my_eth_id !=0) && (my_eth_id != 1) && (my_eth_id != 6) && (my_eth_id != 7)
        && (my_eth_id != 8) && (my_eth_id != 9) && (my_eth_id != 14) && (my_eth_id != 15)) {
      debug_buf[96] = LINK_INACTIVE_PORT_NOT_POPULATED;
      return LINK_NOT_ACTIVE;
    }
  } else if (link_cfg->board_type == NEBULA_X2_RIGHT) {
    if ((my_eth_id !=0) && (my_eth_id != 1) && (my_eth_id != 6) && (my_eth_id != 7)) {
      debug_buf[96] = LINK_INACTIVE_PORT_NOT_POPULATED;
      return LINK_NOT_ACTIVE;
    }
  } else {}

  // Disable ports for custom galaxy topologies
  if ((link_cfg->port_disable_mask >> my_eth_id) & 0x1) {
    debug_buf[96] = LINK_INACTIVE_PORT_MASKED_OFF;
    return LINK_NOT_ACTIVE;
  }

  volatile uint32_t* tmp_2k_buf = (volatile uint32_t*)TEMP_2K_BUF_ADDR;

  // make sure rx queue is initialized immediately at start
  eth_rxq_init(0, 0, 0, false, true);
  eth_rxq_init(1, (((uint32_t)(tmp_2k_buf)) + 1024) >> 4, 1024 >> 4, true,  false);

  // wait constants
  // clk_scaler = 1 => we're running on 27MHz clock, i.e. period = 37ns
  uint32_t phy_power_up_wait_cycles = 544 * clk_counter.aiclk_scalar; // ~20us
  uint32_t reset_propagate_wait_cycles = 16 * clk_counter.aiclk_scalar; // ~600ns
  uint32_t sram_init_done_wait_cycles = 8 * clk_counter.aiclk_scalar; // ~300ns

  // PCS Release Notes (p. 124)
  // 11.1 Initializing the DWC_xlgpcs Core
  // Complete the following steps to initialize the DWC_xlgpcs core:
  // 1. Switch on the power supply. Reset is asserted (pwr_on_rst_n=0).
  // 2. Wait for the required amount of time depending on the PHY requirements.

  eth_wait_cycles(phy_power_up_wait_cycles);  // According to PHY spec (p. 181), need to wait >15us at power-up. Set 20us wait here.

  // 3. De-assert reset (pwr_on_rst_n=1).
  eth_reset_deassert();
  eth_wait_cycles(reset_propagate_wait_cycles);  // Wait for reset deassert to propagate through slow refclk domain

  // 4. Follow the steps mentioned in Programming Steps for SRAM Interface of PHY.

  //     11.2 Programming steps for SRAM Interface of PHY
  eth_pcs_ind_reg_poll(0x1809a, 0x1, 0x1);      // Poll bit [0] (INIT_DN) of VR_PMA_SNPS_MP_25G_16G_SRAM (0x1809A) till it becomes 1.

  // After waiting for a duration of at least 10 clock cycles of cr_para_clk,
  // software can optionally write to CR registers inside DWC_xlgpcs to modify the
  // contents of SRAM connected to Synopsys MP PHY
  eth_wait_cycles(sram_init_done_wait_cycles);
  // if (link_cfg->ovrd_phy_rom_fw) {
  //   volatile uint16_t *p_phy_fw = (volatile uint16_t*)(link_cfg->phy_rom_fw_16kb_offset * 16 * 1024);
  //   for (uint32_t offset = 0; offset < (link_cfg->phy_fw_size_kb*1024)/2; offset++) {
  //     eth_phy_cr_write(ETH_PHY_SRAM_OVRD_START_ADDR + offset, p_phy_fw[offset]);
  //   }
  // }
  eth_pcs_ind_reg_rmw(0x1809a, 0x1, 1, 1);// Program bit [1] (EXT_LD_DN) of VR_PMA_SNPS_MP_25G_16G_SRAM (0x1809A) to 1.

  // 5. Read the SR_PCS_CTRL1(0x30000) registers and wait till the 15th bit of these registers is read as 0.
  //    This indicates that both the PHY and DWC_xlgpcs has successfully completed the reset sequence
  eth_pcs_ind_reg_poll(0x30000, 0x0, 0x1 << 15);
  eth_pcs_ind_reg_write(0x70000, (0x1 << 13)); // disable AN, enable XNP mode

  // tx/rx q - enable raw data mode, used only for post-training crc checks
  uint64_t remote_mac_addr_q1 = 0x0000000000aa;
  eth_txq_reg_write(1, ETH_TXQ_DEST_MAC_ADDR_HI, remote_mac_addr_q1 >> 32);
  eth_txq_reg_write(1, ETH_TXQ_DEST_MAC_ADDR_LO, remote_mac_addr_q1 & 0xFFFFFFFF);
  eth_txq_reg_write(1, ETH_TXQ_CTRL, 0x0);
  eth_txq_reg_write(1, ETH_TXQ_TRANSFER_START_ADDR, (uint32_t)(tmp_2k_buf));
  eth_txq_reg_write(1, ETH_TXQ_TRANSFER_SIZE_BYTES, 1024);

  if (!link_restart) {
    eth_mac_init(link_cfg);
  }

  log_link_bringup_event(link_cfg, EVENT_PCS_PHY_POWERUP, 1);

  if (link_cfg->loopback_mode == LOOPBACK_MAC) { // MAC loopback
    debug_buf[96] = LINK_INACTIVE_MAC_LOOPBACK;
    return LINK_NOT_ACTIVE;
  }
  else if (link_cfg->train_mode == TRAIN_HW_AUTO || link_cfg->train_mode == TRAIN_FW) {
    if (((link_cfg->port_enable_force >> my_node_info->my_eth_id) & 0x1) == 0) {
      return LINK_AN_CFG;
    } else {
      return LINK_NO_AN_START;
    }
  }
  else {
    return LINK_NO_AN_START;
  }
}


eth_link_state_t link_no_an_start_handler(volatile boot_params_t* link_cfg) {

  log_link_bringup_event(link_cfg, EVENT_NO_AN_BRINGUP_START);

  // volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  // 7. If you do not wish to perform auto-negotiation, then you can disable auto-negotiation
  // by programming AN_EN bit of SR_AN_CTRL register to 0. You can then configure
  // DWC_xlgpcs directly to the desired operating speed mode by executing the steps
  // mentioned in sections 11.3 to 11.8.

  eth_pcs_ind_reg_rmw(0x70000, 0x0, 12, 1);  // SR_AN_CTRL

  //  START Section 11.3: Switching to 100G BASE-R mode

  eth_pcs_ind_reg_write(0x10007, 0x2d);

  // 1. To enable RS FEC, program the AM interval period in field [13:0] of VR PCS
  // MMD Alignment Marker Counter Register (VR_PCS_AM_CNT) at address 0x38018;
  // next set bit [2] in RS FEC control register (SR_PMA_RS_FEC_CTRL)
  // at address at address 0x100C8 (If RSFEC is present in the configuration).
  // Note: If you plan to do clause 73 auto-negotiation, do not enable FEC here.
  // Instead, enable it after auto-negotiation (as shown in Figure 11-1), based on the
  // capability negotiated with link -partner.

  if (link_cfg->am_timer_override) {
    eth_pcs_ind_reg_write(0x38018, link_cfg->am_timer_val);
  } else if (link_cfg->rs_fec_en) {
    eth_pcs_ind_reg_write(0x38018, 0x1000); // AM Timer - 4096 for RS FEC enabled 100G modes
  }

  if (link_cfg->rs_fec_en) {
    eth_pcs_ind_reg_write(0x100C8, (0x1 << 2)); // enable RS FEC
  }

  if (link_cfg->suppress_los_det) {
    // FIXME - not in Release Notes sequence, waiting for confirmation from Synopsys
    eth_pcs_ind_reg_write(0x38005, 0x50);   // VR_PCS_DEBUG_CTRL SUPRESS_LOS_DET=1 RX_DT_EN_CTL=1
  }

  // 2. Write 3’b101 to bits [2:0] of SR_PCS_CTRL2 (0x30007).
  eth_pcs_ind_reg_rmw(0x30007, 0x5, 0, 3);

  // 3. Write 4’b0100 to bits [5:2] of SR_PCS_CTRL1 (0x30000)
  eth_pcs_ind_reg_rmw(0x30000, 0x4, 2, 4);

  // 4. Program the bit [0] and bit [1] of VR_PCS_DIG_CTRL3 (0x38003) to 0 to disable the 50G mode.
  eth_pcs_ind_reg_rmw(0x38003, 0x0, 0, 2);

  // 5. Program various register fields as shown in Table 11-1 to configure PHY common settings.

  // START Table 11-1

  // REF_RANGE [5:3] Set the value of this field in VR_PMA_SNPS_MP_25G_16G_REF_CLK_CTRL (0x18091) register to
  //  3’b011 for reference clock frequency 78.125MHz.
  // REF_CLK_DIV2 [2]: Set the value of this filed in VR_PMA_SNPS_MP_25G_16G_REF_CLK_CTRL (0x18091) register to
  //  0 for reference clock frequency 156.25MHz and 78.125MHz.
  // REF_MPLLB_DIV[14:12]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_REF_CLK_CTRL (0x18091) register to
  //  3’b000 for reference clock frequency 78.125MHz
  eth_pcs_ind_reg_rmw(0x18091, 0x3, 3, 3);
  eth_pcs_ind_reg_rmw(0x18091, 0x0, 2, 1);
  eth_pcs_ind_reg_rmw(0x18091, 0x0, 12, 3); // NOTE - this field is different from PCS databook

  // MPLLB_MULTIPLIER[12:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL0 (0x18074) register to 12’ha5.
  eth_pcs_ind_reg_rmw(0x18074, 0xa5, 0, 12); // NOTE - this field is different from PCS databook

  // MPLLB_DIV_MULT [7:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL2 (0x18076) register to 8’h4.
  // MPLLB_TX_CLK_DIV [14:12]: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL2 (0x18076) register to 3’b001.
  eth_pcs_ind_reg_rmw(0x18076, 0x4, 0, 8);
  eth_pcs_ind_reg_rmw(0x18076, 0x1, 12, 3);  // NOTE - this field is different from PCS databook

  // MPLLB_LOW_BW: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL3 (0x18078) register to 16’h18.
  // eth_pcs_ind_reg_write(0x18078, 0x18);
  eth_pcs_ind_reg_write(0x18078, 0x7);
  // MPLLB_HIGH_BW: Set the value of this field in VR_PMA_SNPS_MP_25G_MPLLB_CTRL4 (0x1807A) register to 16’h18.
  // eth_pcs_ind_reg_write(0x1807A, 0x18);
  eth_pcs_ind_reg_write(0x1807A, 0x7);

  // END Table 11-1

  // 6. Program MPLLA_CAL_DISABLE [15] field of VR_PMA_SNPS_MP_25G_MPLLA_CTRL0 (0x18071) register to 1.
  eth_pcs_ind_reg_rmw(0x18071, 0x1, 15, 1);

  // 7. Program MPLLB_CAL_DISABLE [15]  field of VR_PMA_SNPS_MP_25G_MPLLB_CTRL0 (0x18074) register to 0.
  eth_pcs_ind_reg_rmw(0x18074, 0x0, 15, 1);

  // 8. Program various register fields as shown in Table 11-2 to configure per-lane PHY settings.

  // START Table 11-2

  // VCO_REF_LD_0, VCO_REF_LD_1: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_VCO_CAL_REF0 (0x18096) register to
  //  - 7’h9 for reference clock frequency 78.125MHz.
  eth_pcs_ind_reg_write(0x18096, 0x909);

  // VCO_REF_LD_2, VCO_REF_LD_3: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_VCO_CAL_REF1 (0x18097) register to
  // - 7’h9 for reference clock frequency 78.125MHz.
  eth_pcs_ind_reg_write(0x18097, 0x909);

  // VCO_LD_VAL_0: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_VCO_CAL_LD0 (0x18092) register to
  // - 13’h5cd for reference clock frequency 78.125MHz
  eth_pcs_ind_reg_write(0x18092, 0x5cd);

  // VCO_LD_VAL_1: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_VCO_CAL_LD1 (0x18093) register to
  // - 13’h5cd for reference clock frequency 78.125MHz
  eth_pcs_ind_reg_write(0x18093, 0x5cd);

  // VCO_LD_VAL_2: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_VCO_CAL_LD2 (0x18094) register to
  // - 13’h5cd for reference clock frequency 78.125MHz
  eth_pcs_ind_reg_write(0x18094, 0x5cd);

  // VCO_LD_VAL_3: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_VCO_CAL_LD3 (0x18095) register to
  // - 13’h5cd for reference clock frequency 78.125MHz
  eth_pcs_ind_reg_write(0x18095, 0x5cd);

  // RX0_CDR_PPM_MAX, RX1_CDR_PPM_MAX: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_RX_PPM_CTRL0 (0x18065) register to
  // - 5’h14 for reference clock frequency 78.125MHz.
  eth_pcs_ind_reg_write(0x18065, 0x1414); // NOTE - this field is different from PCS databook

  // RX2_CDR_PPM_MAX, RX3_CDR_PPM_MAX: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_RX_PPM_CTRL1 (0x18066) register to
  // - 5’h14 for reference clock frequency 78.125MHz.
  eth_pcs_ind_reg_write(0x18066, 0x1414); // NOTE - this field is different from PCS databook

  // MPLLB_SEL_0 [4]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_MPLL_CMN_CTRL (0x18070) register to 1.
  // MPLLB_SEL_3_1 [7:5]:  Set the value of this field in VR_PMA_SNPS_MP_25G_16G_MPLL_CMN_CTRL (0x18070) register to 7.
  eth_pcs_ind_reg_rmw(0x18070, 0xF, 4, 4);

  // TX_ALIGN_WD_XFER_0 [0]: Set the value of this field in VR_PMA_SNPS_MP_25G_TX_GEN_CTRL5(0x18046) register to 1.
  // TX_ALIGN_WD_XFER_3_1 [3:1]: Set the value of this field in VR_PMA_SNPS_MP_25G_TX_GEN_CTRL5(0x18046) register to 7.
  // NOTE - this register is different from PCS databook
  eth_pcs_ind_reg_rmw(0x18046, 0xF, 0, 4);

  // TX0_DCC_CTRL_RNG[3:0], TX1_DCC_CTRL_RNG[7:4], TX2_DCC_CTRL_RNG[11:8], TX3_DCC_CTRL_RNG[15:12]:
  // Set the value of this field in VR_PMA_SNPS_MP_25G_TX_DCC_CTRL (0x18047) register to 4’hc
  // NOTE - this register is different from PCS databook
  // eth_pcs_ind_reg_write(0x18047, 0xcccc);
  // TX0_DCC_CTRL_RNG[3:0], TX1_DCC_CTRL_RNG[7:4], TX2_DCC_CTRL_RNG[11:8], TX3_DCC_CTRL_RNG[15:12]:
  eth_pcs_ind_reg_write(0x18047, 0xbbbb);

  // EQ_AFE_RT_0[2:0], EQ_AFE_RT_1[6:4], EQ_AFE_RT_2[10:8], EQ_AFE_RT_3[14:12]:
  // Set the value of these fields in VR_PMA_SNPS_MP_25G_RX_AFE_RATE_CTRL (0x1805D) register to 3’b000.
  // NOTE - this register is different from PCS databook
  // eth_pcs_ind_reg_write(0x1805d, 0x0);
  // EQ_AFE_RT_0[2:0], EQ_AFE_RT_1[6:4], EQ_AFE_RT_2[10:8], EQ_AFE_RT_3[14:12]:
  eth_pcs_ind_reg_write(0x1805d, 0x1111);
  // RX EQ AFE Rate 3n0d
  // eth_phy_cr_write(0x600d, 0x600);

  // RX0_RATE[2:0], RX1_RATE[6:4], RX2_RATE[10:8], RX3_RATE[14:12]:
  // Set the value of these fields in VR_PMA_SNPS_MP_25G_RX_RATE_CTRL (0x18054) register to 3’b000.
  eth_pcs_ind_reg_write(0x18054, 0x0);

  // RX0_DELTA_IQ[11:8]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_IQ_CTRL0 (0x1806A) register to 4’b0011.
  // RX1_DELTA_IQ[11:8]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_IQ_CTRL1 (0x1806B) register to 4’b0011.
  // RX2_DELTA_IQ[11:8]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_IQ_CTRL2 (0x1806C) register to 4’b0011.
  // RX3_DELTA_IQ[11:8]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_IQ_CTRL3 (0x1806D) register to 4’b0011.
  // NOTE - these registers are different from PCS databook
  eth_pcs_ind_reg_rmw(0x1806A, 0x3, 8, 4);
  eth_pcs_ind_reg_rmw(0x1806B, 0x3, 8, 4);
  eth_pcs_ind_reg_rmw(0x1806C, 0x3, 8, 4);
  eth_pcs_ind_reg_rmw(0x1806D, 0x3, 8, 4);

  // CTLE_BOOST_0[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL0 (0x18058) register to 5’h0C.
  // CTLE_BOOST_1[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL1 (0x18059) register to 5’h0C.
  // CTLE_BOOST_2[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL2 (0x1805A) register to 5’h0C.
  // CTLE_BOOST_3[4:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL3 (0x1805B) register to 5’h0C.
  // CTLE_POLE_0[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL0 (0x18058) register to 2’h3.
  // CTLE_POLE_1[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL1 (0x18059) register to 2’h3.
  // CTLE_POLE_2[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL2 (0x1805A) register to 2’h3.
  // CTLE_POLE_3[7:5]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_RX_EQ_CTRL3 (0x1805B) register to 2’h3.
  // eth_pcs_ind_reg_rmw(0x18058, 0x6C, 0, 8);
  // eth_pcs_ind_reg_rmw(0x18059, 0x6C, 0, 8);
  // eth_pcs_ind_reg_rmw(0x1805A, 0x6C, 0, 8);
  // eth_pcs_ind_reg_rmw(0x1805B, 0x6C, 0, 8);
  // eth_pcs_ind_reg_rmw(0x18058, 0x664a, 0, 16);
  // eth_pcs_ind_reg_rmw(0x18059, 0x664a, 0, 16);
  // eth_pcs_ind_reg_rmw(0x1805A, 0x664a, 0, 16);
  // eth_pcs_ind_reg_rmw(0x1805B, 0x664a, 0, 16);
  volatile uint32_t* p_ctle = &link_cfg->override_ctle_lane0;
  for (uint32_t i = 0; i < 4; i++) {
    uint8_t ctle_boost = 0;
    uint8_t ctle_pole = 0;
    uint8_t ctle_vga1_gain = 0;
    uint8_t ctle_vga2_gain = 0;
    uint32_t override_ctle = p_ctle[i];
    // uint32_t override_ctle = 0x10080a;
    if (override_ctle != 0) {
      ctle_boost = (override_ctle >> CTLE_BOOST_SHIFT) & CTLE_BOOST_MASK;
      ctle_pole = (override_ctle >> CTLE_POLE_SHIFT) & CTLE_POLE_MASK;
      ctle_vga1_gain = (override_ctle >> CTLE_VGA1_GAIN_SHIFT) & CTLE_VGA1_GAIN_MASK;
      ctle_vga2_gain = (override_ctle >> CTLE_VGA2_GAIN_SHIFT) & CTLE_VGA2_GAIN_MASK;
      eth_pcs_ind_reg_rmw((0x18058 + i), (ctle_boost | (ctle_pole << 5) | (ctle_vga1_gain << 8) | (ctle_vga2_gain << 12)), 0, 16);
    } else {
      eth_pcs_ind_reg_rmw((0x18058 + i), 0x4a, 0, 16);
    }
  }

  //Tx Misc 1n22
  // eth_phy_cr_write(0x5022, 0xa);
  //apply max vboost on the TX side
  eth_pcs_ind_reg_write(0x18031, 0xf5f0);

  // TX/RX term ctrl 3n22
  eth_phy_cr_write(0x6022, 0xe0);

  // CDR VCO Config
  eth_phy_cr_write(0x6007, 0x1200);

  // Sigdet LF threshold

  // DFE_TAP1_0[7:0], DFE_TAP1_1[15:8]: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_DFE_TAP_CTRL0 (0x1805E) register to 8’hf.
  // DFE_TAP1_2[7:0], DFE_TAP1_3[15:8]: Set the value of these fields in VR_PMA_SNPS_MP_25G_16G_DFE_TAP_CTRL1 (0x1805F) register to 8’hf.
  // eth_pcs_ind_reg_write(0x1805E, 0x0f0f);
  // eth_pcs_ind_reg_write(0x1805F, 0x0f0f);
  eth_pcs_ind_reg_write(0x1805E, 0x0a0a);
  eth_pcs_ind_reg_write(0x1805F, 0x0a0a);

  // LF_TH_0[2:0], LF_TH_1[5:3], LF_TH_2[8:6], LF_TH_3[11:9]: Set the value of these fields in VR_PMA_SNPS_MP_25G_RX_GENCTRL3 (0x18053) register to 3’h4.
  eth_pcs_ind_reg_rmw(0x18053, 0x924, 0, 12);

  // END Table 11-2

  // We reset phy & pcs after these:
  if (link_cfg->loopback_mode != LOOPBACK_OFF) {
    if (link_cfg->loopback_mode == LOOPBACK_PHY) {
      // phy tx->rx loopback
      eth_pcs_ind_reg_rmw(0x18090, 0xF, 0, 4);
    } else if (link_cfg->loopback_mode == LOOPBACK_PHY_EXTERN) {
      // local phy rx->tx loopback
      eth_pcs_ind_reg_rmw(0x18090, 0xF, 4, 4);
    } else if (link_cfg->loopback_mode == LOOPBACK_PCS) {
      // local pcs rx->tx loopback
      eth_pcs_ind_reg_rmw(0x38000, 0x1, 14, 1);
    } else {
      // remote phy/pcs rx->tx loopback - just bring up the local link normally
    }
  }

// uint32_t my_eth_id = my_node_info->my_eth_id;

 // if (link_cfg->lane_invert != 0) {
  //   eth_pcs_ind_reg_rmw(0x18030, link_cfg->tx_lane_invert, 4, 4);
  // }

//   if (my_eth_id == 0) {
//     // Port 0 tx0, rx0
//     // Port 0 tx1, rx1
//     // Port 0 rx3
//     // Port 0 tx2, rx2
//     eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4); // RX 0/1/2/3
//     eth_pcs_ind_reg_rmw(0x38001, 0x7, 4, 3); // TX 0/1/2
//   } else if (my_eth_id == 1) {
//     // Port 1 tx1, rx1
//     // Port 1 rx0
//     // Port 1 tx2, rx2
//     // Port 1 tx3, rx3
//     eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4); // RX 0/1/2/3
//     eth_pcs_ind_reg_rmw(0x38001, 0x7, 5, 3); // TX 1/2/3
//   } else if (my_eth_id == 2) {
//     // Port 2 tx2, rx2
//     // Port 2 tx3, rx3
//     // Port 2 tx0, rx0
//     // Port 2 rx1
//     eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4); // RX 0/1/2/3
//     eth_pcs_ind_reg_rmw(0x38001, 0x1, 4, 1); // TX 0
//     eth_pcs_ind_reg_rmw(0x38001, 0x3, 6, 2); // TX 2/3
//   } else if (my_eth_id == 3) {
//     // Port3 rx1
//     // Port3 tx0, rx0
//     eth_pcs_ind_reg_rmw(0x38001, 0x3, 0, 2); // RX 0/1
//     eth_pcs_ind_reg_rmw(0x38001, 0x1, 4, 1); // TX 0
//   }

//  if (my_eth_id == 4) {
//    eth_pcs_ind_reg_rmw(0x38001, 0x1, 0, 1);
//    eth_pcs_ind_reg_rmw(0x38001, 0x1, 2, 1);
//    eth_pcs_ind_reg_rmw(0x38001, 0x1, 4, 1);
//  } else if (my_eth_id == 5) {
//    eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4);
//  }

  vendor_specific_reset_pcs(clk_counter.aiclk_scalar, link_cfg);

  // 12. Program various register fields as shown in Table 11-3.

  //   START Table 11-3

  volatile uint32_t* p_tx_eq = &link_cfg->override_tx_eq_lane0;
  for (uint32_t i = 0; i < 4; i++) {
    uint8_t tx_eq_main = 0;
    uint8_t tx_eq_pre = 0;
    uint8_t tx_eq_post = 0;
    uint32_t override_tx_eq = p_tx_eq[i];
    if (override_tx_eq != 0) {
      tx_eq_main = (override_tx_eq >> TX_EQ_MAIN_SHIFT) & TX_EQ_MASK;
      tx_eq_pre  = (override_tx_eq >> TX_EQ_PRE_SHIFT) & TX_EQ_MASK;
      tx_eq_post = (override_tx_eq >> TX_EQ_POST_SHIFT) & TX_EQ_MASK;
      // TX_EQ_PRE[5:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL0_LN0/1/2/3 (0x18036/0x18038/0x1803A/0x1803C) register to 6’h0.
      // TX_EQ_MAIN[13:8]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL0_LN0/1/2/3 (0x18036/0x18038/0x1803A/0x1803C) register to 6’h18.
      eth_pcs_ind_reg_write((0x18036 + (i * 2)), (((tx_eq_main & 0x3f) << 8) | (tx_eq_pre & 0x3f)));

      // eth_pcs_ind_reg_write(0x18036, 0x1009);
      // eth_pcs_ind_reg_write(0x18038, 0x1009);
      // eth_pcs_ind_reg_write(0x1803A, 0x1009);
      // eth_pcs_ind_reg_write(0x1803C, 0x1009);

      // TX_EQ_POST[5:0]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL1_LN0/1/2/3 (0x18037/0x18039/0x1803B/0x1803D) register to 6’h0.
      // TX_EQ_OVER_RIDE[6]: Set the value of this field in VR_PMA_SNPS_MP_25G_16G_TX_EQ_CTRL1_LN0/1/2/3 (0x18037/0x18039/0x1803B/0x1803D) register to 1 when “DWCXLP_LNK_TRAIN_EN” parameter is set.
      eth_pcs_ind_reg_write((0x18037 + (i * 2)), ((0x1 << 6) | (tx_eq_post & 0x3f)));

      // eth_pcs_ind_reg_write(0x18037, 0x42);
      // eth_pcs_ind_reg_write(0x18039, 0x42);
      // eth_pcs_ind_reg_write(0x1803B, 0x42);
      // eth_pcs_ind_reg_write(0x1803D, 0x42);
    } else {
      // eth_pcs_ind_reg_write((0x18036 + (i * 2)), 0x1009);
      // eth_pcs_ind_reg_write((0x18037 + (i * 2)), 0x5a);
      // Optimum Tx taps settings for Credo
      eth_pcs_ind_reg_write((0x18036 + (i * 2)), 0x1210);
      eth_pcs_ind_reg_write((0x18037 + (i * 2)), 0x48);
    }
  }

  //  END Table 11-3

  //  END Section 11.3

  // Thereafter, wait for RLU bit [2] of the SR_PCS_STS1 (0x30001) register to be set to 1
  uint64_t curr_time = 0;
  uint64_t next_1ms_time = 0;
  uint32_t post_training_time_ms = 0;
  uint16_t sigdet_read_val = 0;
  uint16_t rxlink_read_val = 0;

  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  uint32_t timeout_limit = !no_an_initial_attempt ? STATIC_RX_LNKUP_TIMEOUT_INITIAL : STATIC_RX_LNKUP_TIMEOUT_RESTART;
  eth_link_state_t status = LINK_RESTART_CHECK;

  // Note: This bit denotes ‘Rx link up’ and hence will get asserted, only if DWC_xlgpcs is
  // receiving data at proper data-rates from the link partner.
  // The DWC_xlgpcs core is now ready to transmit or receive data.
  next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
  do {
    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      post_training_time_ms++;
    }
    rxlink_read_val = (eth_pcs_ind_reg_read(0x30001) & 0x4);
    sigdet_read_val |= (eth_pcs_ind_reg_read(0x1000a) & 0x1f);
  } while ((rxlink_read_val != 0x4) && (post_training_time_ms <= timeout_limit));
  test_results[45] = post_training_time_ms;

  log_link_bringup_event(link_cfg, EVENT_NO_AN_BRINGUP_DONE);

  if (link_cfg->test_mode == TEST_MODE_PHY_PRBS) status = LINK_TEST_MODE;

  if (link_cfg->loopback_mode != LOOPBACK_OFF) {
    debug_buf[96] = LINK_INACTIVE_PHY_LOOPBACK;
    status = LINK_NOT_ACTIVE;
  }

  if (!no_an_initial_attempt && (sigdet_read_val != 0x1f)) {
    // Check sig detect status
    log_link_bringup_event(link_cfg, EVENT_LINK_SIGDET_FAILED, sigdet_read_val);
    debug_buf[96] = LINK_INACTIVE_TIMEOUT_SIGDET;
    status = LINK_NOT_ACTIVE;
  }

  // Default to running packet data test after static training
  if ((status == LINK_RESTART_CHECK) && (link_cfg->test_mode != TEST_MODE_PACKET_DATA) && (link_cfg->test_mode != TEST_MODE_PACKET_BANDWIDTH)) {
    link_cfg->test_mode = TEST_MODE_PACKET_DATA;
  }

  // Set the initial attempt flag
  no_an_initial_attempt = 1;

  return status;
}


eth_link_state_t link_an_cfg_handler(volatile boot_params_t* link_cfg) {
  uint32_t my_eth_id = my_node_info->my_eth_id;

  curr_an_attempts = 0;

  override_phy_default_settings(link_cfg);

  //lower the SIGDET threshold to 0x2
  eth_pcs_ind_reg_rmw(0x18053, 0x924, 0, 12);
  //apply max vboost on the TX side
  eth_pcs_ind_reg_write(0x18031, 0xf7f0);

  if (link_cfg->am_timer_override) {
    eth_pcs_ind_reg_write(0x38018, link_cfg->am_timer_val);
  } else if (link_cfg->rs_fec_en) {
    eth_pcs_ind_reg_write(0x38018, 0x1000); // AM Timer - 4096 for RS FEC enabled 100G modes
  }

  if (link_cfg->rs_fec_en) {
    eth_pcs_ind_reg_write(0x100C8, (0x1 << 2)); // enable RS FEC
  }

  if (link_cfg->suppress_los_det) {
    eth_pcs_ind_reg_write(0x38005, 0x70);   // VR_PCS_DEBUG_CTRL
  }

  // VR_PMA_Gen5_12G_RX_EQ_CTRL4
  // Disable cont adapt and offset can prior to link training
  eth_pcs_ind_reg_rmw(0x1805c, 0, 0, 4);
  eth_pcs_ind_reg_rmw(0x1805c, 0, 4, 4);

  if (link_cfg->ping_pong_alg_mode != 0) {
    eth_pcs_ind_reg_rmw(0x1805c, link_cfg->ping_pong_alg_mode, 8, 3);
  }

  if (link_cfg->tx_eq_preset_en) {
    eth_pcs_ind_reg_rmw(0x18037, 0x1, 7, 1);
    eth_pcs_ind_reg_rmw(0x18039, 0x1, 7, 1);
    eth_pcs_ind_reg_rmw(0x1803B, 0x1, 7, 1);
    eth_pcs_ind_reg_rmw(0x1803D, 0x1, 7, 1);
  }

  if (link_cfg->rx_eq_preset_en) {
    eth_pcs_ind_reg_rmw(0x1805c, 0x1, 11, 1); // preset
  }

  // uint32_t my_eth_id = my_node_info->my_eth_id;

  // if (my_eth_id == 2) {
  //   eth_pcs_ind_reg_rmw(0x38001, 0x1, 0, 1);
  //   eth_pcs_ind_reg_rmw(0x38001, 0x1, 4, 1);
  // }

//   if (my_eth_id == 0) {
//     // Port 0 tx0, rx0
//     // Port 0 tx1, rx1
//     // Port 0 rx3
//     // Port 0 tx2, rx2
//     eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4); // RX 0/1/2/3
//     eth_pcs_ind_reg_rmw(0x38001, 0x7, 4, 3); // TX 0/1/2
//   } else if (my_eth_id == 1) {
//     // Port 1 tx1, rx1
//     // Port 1 rx0
//     // Port 1 tx2, rx2
//     // Port 1 tx3, rx3
//     eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4); // RX 0/1/2/3
//     eth_pcs_ind_reg_rmw(0x38001, 0x7, 5, 3); // TX 1/2/3
//   } else if (my_eth_id == 2) {
//     // Port 2 tx2, rx2
//     // Port 2 tx3, rx3
//     // Port 2 tx0, rx0
//     // Port 2 rx1
//     eth_pcs_ind_reg_rmw(0x38001, 0xf, 0, 4); // RX 0/1/2/3
//     eth_pcs_ind_reg_rmw(0x38001, 0x1, 4, 1); // TX 0
//     eth_pcs_ind_reg_rmw(0x38001, 0x3, 6, 2); // TX 2/3
//   } else if (my_eth_id == 3) {
//     // Port3 rx1
//     // Port3 tx0, rx0
//     eth_pcs_ind_reg_rmw(0x38001, 0x3, 0, 2); // RX 0/1
//     eth_pcs_ind_reg_rmw(0x38001, 0x1, 4, 1); // TX 0
//   }

  vendor_specific_reset_pcs(clk_counter.aiclk_scalar, link_cfg);

  eth_pcs_ind_reg_write(0x78001, 0x7); // clear any an interrupts
  if (link_cfg->rs_fec_en) {
    eth_pcs_ind_reg_write(0x70012, 0x1 << 12); // advertise RS FEC
  }

  // enable XNP mode in SR_AN_ADV1 register
  eth_pcs_ind_reg_rmw(0x70010, 0x1, 15, 1);

  // AN timers
  // - value = (value_written * 2048) + 750
  // - clk period is ~2.56ns (div33 clk)
  // - effective time value = (value_written * 5.24us) + 1.92us
  // eth_pcs_ind_reg_write(0x78004, ??);  // break link timer - default = 60ms
  // eth_pcs_ind_reg_write(0x78005, ??);  // link fail inhibit/autoneg wait timer - default 500ms (seems like it can't be overridden with higher values)

  // Link training timers
  eth_pcs_ind_reg_write(0x18007, 0xffff);
  // eth_pcs_ind_reg_write(0x18006, ??); // krtr timer0 = (value * 4096) + 'hb74 = value*10.5us + 7.5us, default = 500ms
  eth_pcs_ind_reg_write(0x18008, 57252); // krtr timer2 = (value * 2048) + 'h178 = value*5.24us + 1us = 300ms
  // eth_pcs_ind_reg_write(0x18008, 76334); // krtr timer2 = (value * 2048) + 'h178 = value*5.24us + 1us = 400ms

  if (link_cfg->lt_const_amp_alg) {
    eth_pcs_ind_reg_rmw(0x18037, 0x1, 8, 1);
    eth_pcs_ind_reg_rmw(0x18039, 0x1, 8, 1);
    eth_pcs_ind_reg_rmw(0x1803B, 0x1, 8, 1);
    eth_pcs_ind_reg_rmw(0x1803D, 0x1, 8, 1);
  }

  // PRBS
  if (link_cfg->prbs_en) {
    // eth_pcs_ind_reg_write(0x18004, 0xffee);  // prbs timer - resolution = ~82ns, default ~3ms
    eth_pcs_ind_reg_write(0x18005, link_cfg->prbs_max_errors);
    eth_pcs_ind_reg_write(0x18003, 0x1);
  }

  // FW link training
  if (link_cfg->train_mode == TRAIN_FW) {
    for (int l = 0; l < 4; l++) {
      eth_pcs_ind_reg_write(0x180b4+l, (0x1 << 15)); // VR_PMA_KRTR_TX_EQ_STS_CTRLx
      eth_pcs_ind_reg_write(0x180bc+l, (0x1 << 15)); // VR_PMA_KRTR_RX_EQ_CTRLx
    }
  }

  // default: 5, 64, 26
  // 24 = main + pre/4 + post/4
  uint32_t pre_init = 0x5;
  uint32_t main_init = 0x40;
  uint32_t post_init = 0x1a;

  if (((my_eth_id > 3) && (my_eth_id < 9)) || (my_eth_id > 11)) {
    // alternate the tx eq settings to default every 5 cycles of AN attempts
    // to recover over compensated tx eq settings

    if (((an_attempts / 5) % 2) == 1) {
        pre_init = 0x5;
        main_init = 0x40;
        post_init = 0x1a;
    } else {
      if ((link_cfg->board_type == GALAXY) && ((my_eth_id == 8) || (my_eth_id == 15))) {
        pre_init = 0x18;
        main_init = 0x10;
        post_init = 0x8;
      } else {
        pre_init = 0xc;
        main_init = 0x12;
        post_init = 0xc;
      }
    }
    eth_pcs_ind_reg_write(0x18080, ((main_init & 0xff) << 8) | (pre_init & 0x3f));
    eth_pcs_ind_reg_write(0x18081, (post_init & 0x3f));
  }

  eth_pcs_ind_reg_write(0x10002, 0xcdef); // dev_id

  // uint32_t wait_cycles_100ms = 27000 * link_cfg->clk_scaler * 100; //~100ms

  // eth_wait_cycles(wait_cycles_100ms);

  return LINK_AN_RESTART;
}


eth_link_state_t link_an_restart_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  an_attempts++;

  if (an_attempts > link_cfg->timeout_an_attemps) {
    debug_buf[96] = LINK_INACTIVE_TIMEOUT_AN_ATTEMPTS;
    return LINK_NOT_ACTIVE;
  }

  if ((link_cfg->train_mode == TRAIN_STATIC) || (((link_cfg->port_enable_force >> my_node_info->my_eth_id) & 0x1) == 1)) {
    return LINK_PCS_RESET;
  }

  if (curr_an_attempts == link_cfg->an_attempts_before_pcs_reset) {
    curr_an_attempts = 0;
    log_link_bringup_event(link_cfg, EVENT_PCS_LINK_ON, 0);
    return LINK_PCS_RESET;
  } else {
    curr_an_attempts++;
    debug_buf[1] = curr_an_attempts;
  }

  eth_pcs_ind_reg_write(0x10096, 0); // disable lt, enable & restart after the next AN page receive
  eth_pcs_ind_reg_write(0x70000, (0x1 << 13) | (0x1 << 12) | (0x1 << 9)); // enable & restart AN, XNP mode
  eth_pcs_ind_reg_write(0x78002, 0); // clear any pending AN interrupt

  log_link_bringup_event(link_cfg, EVENT_AN_RESTART);

  eth_pcs_ind_reg_rmw(0x38000, 0x3, 8, 2); // reset pcs tx and rx path
  eth_pcs_ind_reg_poll(0x38000, 0x0, 0x300); // according to pcs databook, need to poll for these bits to go back to 0 before further operations

  __attribute__((unused)) uint32_t tmp_val; // read misc. pcs error registers to clear
  tmp_val = eth_pcs_ind_reg_read(0x100ca);
  tmp_val = eth_pcs_ind_reg_read(0x100cb);
  tmp_val = eth_pcs_ind_reg_read(0x100cc);
  tmp_val = eth_pcs_ind_reg_read(0x100cd);
  for (int l = 0; l < 4; l++) {
    tmp_val = eth_pcs_ind_reg_read(0x100d2+l*2);
    tmp_val = eth_pcs_ind_reg_read(0x100d3+l*2);
    tmp_val = eth_pcs_ind_reg_read(0x38019);
  }
  tmp_val = eth_pcs_ind_reg_read(0x30021);
  tmp_val = eth_pcs_ind_reg_read(0x78002);
  tmp_val = eth_pcs_ind_reg_read(0x38020);

  uint32_t mac_mmc_ctrl_val = eth_mac_reg_read(0x800);
  eth_mac_reg_write(0x800, mac_mmc_ctrl_val | 0x1); // reset all mac counters

  return LINK_AN_PG_RCV;
}


eth_link_state_t link_an_pg_rcv_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  uint32_t an_intr;
  uint32_t ext_pg_sig = 0;
  uint64_t curr_time = 0;
  uint64_t next_1ms_time = 0;
  bool timeout = false;
  uint32_t training_time_ms = 0;

  next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
  do {
    an_intr = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR

    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      training_time_ms++;
    }
    timeout = (training_time_ms > link_cfg->pg_rcv_wait_timeout_ms); // timeout if the other side enters link inactive state (default ~500ms)
  } while ((an_intr == 0) && !timeout);

  eth_pcs_ind_reg_write(0x78002, 0); // clear interrupt

  if (an_intr != 0x4) { // inc link, this will be received on timeout, maybe also a stray an complete
    log_link_bringup_event(link_cfg, EVENT_AN_INTR_NO_PG_RCV, an_intr);
    pg_rcv_fail++;

    // LINK_AN_RESTART on every 10th cycle to avoid unnecessary link restarts when modules are booted at different times
    if (pg_rcv_fail > link_cfg->pg_rcv_fail_limit) {
      debug_buf[96] = LINK_INACTIVE_TIMEOUT_PG_RCV;
      return LINK_NOT_ACTIVE;
    } else if ((pg_rcv_fail != 0) && ((pg_rcv_fail % 10) == 0)) {
      // allow LINK_AN_RESTART every 10th cycle
      return LINK_AN_RESTART;
    } else {
      // repeat LINK_AN_PG_RCV to avoid unnecessary link restarts when modules are booted at different times
      return LINK_AN_PG_RCV;
    }
  } else {
    // clear pg_rcv_fail if valid page is received
    pg_rcv_fail = 0;

    // send extended next page with 0xF3 signature
    eth_pcs_ind_reg_write(0x70018, 0);
    eth_pcs_ind_reg_write(0x70017, 0);
    eth_pcs_ind_reg_write(0x70016, 0xF3);

    // wait for next page received
    training_time_ms = 0;
    next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
    do {
      an_intr = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR

      if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
        next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
        training_time_ms++;
      }

      timeout = (training_time_ms > link_cfg->pg_rcv_wait_timeout_ms); // timeout if the other side enters link inactive state (default ~500ms)
    } while ((an_intr == 0) && !timeout);

    eth_pcs_ind_reg_write(0x78002, 0); // clear interrupt

    // if the next page received is not valid, restart an_pg_rcv
    ext_pg_sig = eth_pcs_ind_reg_read(0x70019);
    if ((ext_pg_sig & 0xF3) != 0xF3) {
      return LINK_AN_PG_RCV;
    }

    // VR_PMA_Gen5_12G_RX_EQ_CTRL4
    if (!link_cfg->disable_cont_adapt) {
      eth_pcs_ind_reg_rmw(0x1805c, 0xf, 0, 4);
    }
    if (!link_cfg->disable_cont_off_can) {
      eth_pcs_ind_reg_rmw(0x1805c, 0xf, 4, 4);
    }
    debug_buf[2] = eth_pcs_ind_reg_read(0x70013);
    debug_buf[3] = eth_pcs_ind_reg_read(0x70014);
    debug_buf[4] = eth_pcs_ind_reg_read(0x70015);
    debug_buf[5] = eth_pcs_ind_reg_read(0x10097);

    log_link_bringup_event(link_cfg, EVENT_AN_INTR_PG_RCV, an_intr);
    return link_cfg->train_mode == TRAIN_FW ? LINK_TRAINING_FW : LINK_TRAINING;
  }
}


inline uint32_t get_training_timer_ms_lane(uint32_t l) {
  //longest delay is nedded for lane 0
  //lane 0 - 406; lane 1 - 404; lane 2 - 402; lane 3 - 400
  return 406 - ((l % 4) * 2);
}


eth_link_state_t link_training_fw_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  uint32_t training_time_ms = 0;
  uint32_t total_iters = 0;
  uint32_t total_iters_last_1ms = 0;

  for (uint32_t l = 0; l < 4; l++) {

    curr_lane_tx_eq_state[l] = TX_EQ_WAIT_UPDATE_CMD;
    curr_lane_rx_eq_state[l] = RX_EQ_ISSUE_INIT;

    lane_training_frame_locked_iters[l] = 0;
    lane_training_frame_locked_iters_last_1ms[l] = 0;
    lane_frame_lock[l] = false;

    tx_eq_lp_ceu[l] = 0;
    tx_eq_ld_sts[l] = 0;
    tx_eq_phy_update_req[l] = 0;
    // rx_eq_ongoing_update_req[l] = 0;
    rx_eq_ld_ceu[l] = 0;
    rx_eq_lp_sts[l] = 0;
  }


  // uint32_t tx_pre;
  // uint32_t tx_post;
  // uint32_t tx_main;

  //  for (int l = 0; l < 4; l++) {
  //
  //    uint32_t tx_asic_in1 = eth_phy_cr_read(0x1016 + (l << 8));
  //    uint32_t tx_asic_in2 = eth_phy_cr_read(0x1017 + (l << 8));
  //    tx_pre = tx_asic_in2 & 0x3F; // pre
  //    tx_post = (tx_asic_in2 >> 6) & 0x3F; // post
  //    tx_main = (tx_asic_in1 >> 8) & 0x3F; // main
  //    debug_buf[22 + 3*l] = tx_pre;
  //    debug_buf[22 + 3*l + 1] = tx_post;
  //    debug_buf[22 + 3*l + 2] = tx_main;
  //  }


  eth_pcs_ind_reg_write(0x10096, 0x2);
  eth_pcs_ind_reg_write(0x10096, 0x3); // lt enable & restart

  log_link_bringup_event(link_cfg, EVENT_LINK_TRAINING_START, 1);

  bool all_lanes_ok = false;
  uint32_t all_lanes_status = 0;
  uint64_t curr_time = 0;
  uint64_t next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);

  while (training_time_ms < 600) {
    total_iters++;
    debug_buf[37] = total_iters;

    // bool tick_1ms = false;
    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      training_time_ms++;
      // tick_1ms = true;
      uint32_t iters_1ms = total_iters - total_iters_last_1ms;
      total_iters_last_1ms = total_iters;
      for (uint32_t l = 0; l < 4; l++) {
        uint32_t lane_frame_locked_iters_1ms = lane_training_frame_locked_iters[l] - lane_training_frame_locked_iters_last_1ms[l];
        lane_training_frame_locked_iters_last_1ms[l] = lane_training_frame_locked_iters[l];
        // if (false && link_cfg->link_bringup_log_verbose) {
          // log_link_bringup_event(link_cfg, EVENT_LANE_FRAME_LOCK_MONITOR, l, lane_frame_locked_iters_1ms, iters_1ms);
        // } else  {
        bool next_frame_lock = ((iters_1ms - lane_frame_locked_iters_1ms) < (iters_1ms/16));
        if (next_frame_lock != lane_frame_lock[l]) {
          lane_frame_lock[l] = next_frame_lock;
          // log_link_bringup_event(link_cfg, EVENT_LANE_FRAME_LOCK_STATUS, l, training_time_ms, (uint32_t)next_frame_lock);
        }
        if (lane_frame_locked_iters_1ms == 0) {
          log_link_bringup_event(link_cfg, EVENT_LANE_FRAME_LOCK_LOST);
        }
        // }
      }
    }

    uint32_t new_all_lanes_status = eth_pcs_ind_reg_read(0x10097);
    all_lanes_ok = true;

    // if (link_cfg->link_bringup_log_verbose && (new_all_lanes_status != all_lanes_status)) {
    //   log_link_bringup_event(link_cfg, EVENT_LANE_STATUS_UPDATE, new_all_lanes_status);
    // }
    all_lanes_status = new_all_lanes_status;

    // eth_ctrl_reg_write(ETH_CTRL_SCRATCH2, 0xC0000000 | training_time_ms);
    // eth_ctrl_reg_write(ETH_CTRL_SCRATCH3, 0xD0000000 | all_lanes_status);

    for (uint32_t l = 0; l < 4; l++) {

      uint32_t lane_status = (all_lanes_status >> (4*l)) & 0xF;
      uint32_t fail = (lane_status >> 3) & 0x1;
      uint32_t frame_lock = (lane_status >> 1) & 0x1;
      all_lanes_ok &= (!fail);
      lane_training_frame_locked_iters[l] += frame_lock;

      next_lane_tx_eq_state[l] = curr_lane_tx_eq_state[l];
      next_lane_rx_eq_state[l] = curr_lane_rx_eq_state[l];

      // // FIXME imatosevic - what to do for recovery when frame lock = 0?

      // //  *** TX EQ FSM ***

      switch (curr_lane_tx_eq_state[l]) {

      case TX_EQ_WAIT_UPDATE_CMD: {
        // Software polls for INC/DEC/PRESET/INIT command (received from link partner) in SR_PMA_KR_LP_CEU Register
        tx_eq_lp_ceu[l] = eth_pcs_ind_reg_read(l+0x1044c);
        debug_buf[38+l] = tx_eq_lp_ceu[l];
        if (tx_eq_lp_ceu[l] != 0) {
          // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LP_UPDATE, l, tx_eq_lp_ceu[l]);
          if (link_cfg->link_training_fw_phony) {
            // Just return status without updating anything
            if (tx_eq_lp_ceu[l] & (0x3 << 12)) { // preset/initialize
              tx_eq_ld_sts[l] = 0x15;
            }
            else { // coeff update
              tx_eq_ld_sts[l] = 0;
              for (uint32_t c = 0; c < 3; c++) {
                uint32_t c_upd = (tx_eq_lp_ceu[l] >> (2*c)) & 0x3;
                if (c_upd) {
                  tx_eq_ld_sts[l] |= (0x1 << (2*c));
                }
              }
            }
            eth_pcs_ind_reg_write(0x180b4+l, (0x1 << 15) | tx_eq_ld_sts[l]);
            // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LD_STATUS, l, tx_eq_ld_sts[l]);
            next_lane_tx_eq_state[l] = TX_EQ_WAIT_HOLD_CMD;
          }
          else {
            // Software writes INC/DEC/PRESET/INIT command to VR_PMA_KRTR_TX_EQ_CFF_CTRL Register
            eth_pcs_ind_reg_write(l+0x180b0, tx_eq_lp_ceu[l]);
            tx_eq_phy_update_req[l] = tx_eq_lp_ceu[l];
            next_lane_tx_eq_state[l] = TX_EQ_WAIT_PHY_UPDATE_STS;
          }
        }
        else if (rx_eq_lp_sts[l] & (0x1 << 15)) {
          // link partner sent "receiver ready"
          next_lane_tx_eq_state[l] = TX_EQ_DONE;
        }
        break;
      }

      case TX_EQ_WAIT_PHY_UPDATE_STS: {
        // Software waits for ‘Status’ given by local PHY in VR_PMA_PHY_TX_EQ_STS Register (Wait for STSM1_VLD/STS0_VLD/STS1_VLD fields to go high )
        uint32_t phy_tx_eq_sts = eth_pcs_ind_reg_read(l+0x180b8);
        debug_buf[42+l] = phy_tx_eq_sts;
        bool phy_sts_vld;
        bool phy_all_sts_vld;
        if (tx_eq_phy_update_req[l] & (0x1 << 13)) { // preset
          // The preset control shall only be initially sent when all coefficient status fields indicate not_updated,
          // and will then continue to be sent until the status for all coefficients indicates updated or maximum."
          phy_sts_vld = (((phy_tx_eq_sts >> 8) & 0x7) == 0x7);
          phy_all_sts_vld = phy_sts_vld;
          tx_eq_ld_sts[l] = phy_tx_eq_sts & 0x3F;
        }
        else if (tx_eq_phy_update_req[l] & (0x1 << 12)) { // initialize
          // "The initialize control shall only be initially sent when all coefficient status fields indicate not_updated,
          // and will then continue to be sent until no coefficient status field indicates not_updated."
          phy_sts_vld = (((phy_tx_eq_sts >> 8) & 0x7) == 0x7);
          phy_all_sts_vld = phy_sts_vld;
          tx_eq_ld_sts[l] = phy_tx_eq_sts & 0x3F;
        }
        else { // coeff update
          phy_sts_vld = false;
          phy_all_sts_vld = true;
          for (uint32_t c = 0; c < 3; c++) {
            uint32_t c_upd = (tx_eq_phy_update_req[l] >> (2*c)) & 0x3;
            uint32_t c_sts_vld = (phy_tx_eq_sts >> (8+c)) & 0x1;
            phy_all_sts_vld &= (!c_upd || c_sts_vld);
            if (c_upd && c_sts_vld) {
              uint32_t c_sts = (phy_tx_eq_sts >> (2*c)) & 0x3;
              phy_sts_vld = true;
              tx_eq_ld_sts[l] |= (c_sts << (2*c));
              tx_eq_phy_update_req[l] &= ~(0x3 << (2*c));
              eth_pcs_ind_reg_write(l+0x180b0, tx_eq_phy_update_req[l]);
            }
          }
        }
        debug_buf[46+l] = tx_eq_phy_update_req[l];
        if (phy_sts_vld) {
          // Software writes a UPDATED/MIN/MAX Status to VR_PMA_KRTR_TX_EQ_STS_CTRL Register
          eth_pcs_ind_reg_write(0x180b4+l, (0x1 << 15) | tx_eq_ld_sts[l]);
          // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LD_STATUS, l, tx_eq_ld_sts[l]);
        }
        if (phy_all_sts_vld) {
          next_lane_tx_eq_state[l] = TX_EQ_WAIT_HOLD_CMD;
        }
        break;
      }

      case TX_EQ_WAIT_HOLD_CMD: {

        // Software polls for HOLD command (received from link partner) in SR_PMA_KR_LP_CEU Register
        uint32_t next_lp_ceu = eth_pcs_ind_reg_read(l+0x1044c);
        debug_buf[50+l] = next_lp_ceu;
        if (next_lp_ceu != tx_eq_lp_ceu[l]) {
          // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LP_UPDATE, l, next_lp_ceu);
          tx_eq_lp_ceu[l] = next_lp_ceu;
        }
        if (next_lp_ceu == 0) {
          if (link_cfg->link_training_fw_phony) {
            eth_pcs_ind_reg_write(0x180b4+l, (0x1 << 15));
            tx_eq_ld_sts[l] = 0;
            // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LD_STATUS, l, tx_eq_ld_sts[l]);
            next_lane_tx_eq_state[l] = TX_EQ_WAIT_UPDATE_CMD;
          } else {
            // Software writes HOLD command to VR_PMA_KRTR_TX_EQ_CFF_CTRL Register
            eth_pcs_ind_reg_write(l+0x180b0, 0);
            next_lane_tx_eq_state[l] = TX_EQ_WAIT_PHY_HOLD_STS;
          }
        }
        break;
      }

      case TX_EQ_WAIT_PHY_HOLD_STS: {

        // Software waits for ‘STSM1_VLD/STS0_VLD/STS1_VLD’ bits in VR_PMA_PHY_TX_EQ_STS Register to go low
        uint32_t phy_tx_eq_sts = eth_pcs_ind_reg_read(l+0x180b8);
        debug_buf[54+l] = phy_tx_eq_sts;
        if (((phy_tx_eq_sts >> 8) & 0x7) == 0) {
          // Software writes a NOT_UPDATED Status to VR_PMA_KRTR_TX_EQ_STS_CTRL Register
          eth_pcs_ind_reg_write(0x180b4+l, (0x1 << 15));
          tx_eq_ld_sts[l] = 0;
          // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LD_STATUS, l, tx_eq_ld_sts[l]);
          next_lane_tx_eq_state[l] = TX_EQ_WAIT_UPDATE_CMD;
        }
        break;
      }

      case TX_EQ_DONE: {
        uint32_t next_lp_ceu = eth_pcs_ind_reg_read(l+0x1044c);
        debug_buf[58+l] = next_lp_ceu;
        if (tx_eq_lp_ceu[l] != next_lp_ceu) {
          tx_eq_lp_ceu[l] = next_lp_ceu;
          // log_link_bringup_event(link_cfg, EVENT_TX_EQ_LP_UPDATE, l, tx_eq_lp_ceu[l]);
        }
        break;
      }

      } // switch (curr_lane_tx_eq_state[l])


      // *** RX EQ FSM ***

      switch (curr_lane_rx_eq_state[l]) {

      case RX_EQ_ISSUE_INIT: {
        // Software programs ‘INC/DEC/PRESET/INIT’ command to VR_PMA_KRTR_RX_EQ_CTRL Register
        eth_pcs_ind_reg_write(0x180bc+l, (0x1 << 15) | (0x1 << 6)); // set INIT
        rx_eq_ld_ceu[l] = (0x1 << 6);
        // log_link_bringup_event(link_cfg, EVENT_RX_EQ_LD_UPDATE, l, (0x1 << 6));
        next_lane_rx_eq_state[l] = RX_EQ_WAIT_UPDATE_STS;
        break;
      }

      case RX_EQ_ISSUE_ADAPT_REQ: {
        if (training_time_ms >= get_training_timer_ms_lane(l)) {
          next_lane_rx_eq_state[l] = RX_EQ_ISSUE_FINAL_ADAPT_REQ;
        } else {
          // Rx Adaptation Request - RX_AD_REQ bit -> RTL not consistent with spec, there are actually 4 separate bits for 4 lanes
          eth_pcs_ind_reg_rmw(0x1805c, 1, 12+l, 1);
          next_lane_rx_eq_state[l] = RX_EQ_WAIT_PHY_UPDATE_CMD;
        }
        break;
      }

      case RX_EQ_WAIT_PHY_UPDATE_CMD: {
        // Software Polls for coefficient update command (given by local PHY) in VR_PMA_KRTR_RX_EQ_CEU Register
        uint32_t phy_ceu = eth_pcs_ind_reg_read(0x180c4+l);
        debug_buf[62+l] = phy_ceu;
        if ((phy_ceu >> 8) & 0x7) { // from the pcs logic, it seems all 3 bits are tied to the same lane adapt ack signal
          // Software programs ‘INC/DEC/PRESET/INIT’ command to VR_PMA_KRTR_RX_EQ_CTRL Register
          rx_eq_ld_ceu[l] = phy_ceu & 0x3F;
          eth_pcs_ind_reg_write(0x180bc+l, (0x1 << 15) | rx_eq_ld_ceu[l]);
          // log_link_bringup_event(link_cfg, EVENT_RX_EQ_LD_UPDATE, l, rx_eq_ld_ceu[l]);
          next_lane_rx_eq_state[l] = RX_EQ_WAIT_UPDATE_STS;
        }
        break;
      }

      case RX_EQ_WAIT_UPDATE_STS: {
        // Software polls for UPDATED/MIN/MAX Status (sent by link partner) in SR_PMA_KR_LP_STS Register
        uint32_t next_lp_sts = eth_pcs_ind_reg_read(l+0x104b0);
        debug_buf[66+l] = next_lp_sts;
        if (next_lp_sts != rx_eq_lp_sts[l]) {
          rx_eq_lp_sts[l] = next_lp_sts;
          // log_link_bringup_event(link_cfg, EVENT_RX_EQ_LP_STATUS, l, rx_eq_lp_sts[l]);
        }
        bool all_c_sts = true;
        for (uint32_t c = 0; c < 3; c++) {
          uint32_t c_sts = (rx_eq_lp_sts[l] >> (2*c)) & 0x3;
          all_c_sts &= c_sts;
          if ((rx_eq_ld_ceu[l] >> (2*c)) & 0x3) {
            if (c_sts) {
              // Software programs ‘HOLD’ command to VR_PMA_KRTR_RX_EQ_CTRL Register
              rx_eq_ld_ceu[l] &= ~(0x3 << (2*c));
              eth_pcs_ind_reg_write(0x180bc+l, (0x1 << 15) | rx_eq_ld_ceu[l]);
              // log_link_bringup_event(link_cfg, EVENT_RX_EQ_LD_UPDATE, l, rx_eq_ld_ceu[l]);
            }
          }
        }
        if (rx_eq_ld_ceu[l] & (0x3 << 6)) { // preset/init
          if (all_c_sts) {
            rx_eq_ld_ceu[l] = 0;
            eth_pcs_ind_reg_write(0x180bc+l, (0x1 << 15) | rx_eq_ld_ceu[l]);
            // log_link_bringup_event(link_cfg, EVENT_RX_EQ_LD_UPDATE, l, rx_eq_ld_ceu[l]);
          }
        }
        if (rx_eq_ld_ceu[l] == 0) {
          next_lane_rx_eq_state[l] = RX_EQ_WAIT_HOLD_STS;
        }
        break;
      }

      case RX_EQ_WAIT_HOLD_STS: {
        // Software polls for NOT-UPDATED Status (send by link partner) in SR_PMA_KR_LP_STS Register
        uint32_t next_lp_sts = eth_pcs_ind_reg_read(l+0x104b0);
        debug_buf[70+l] = next_lp_sts;
        if (next_lp_sts != rx_eq_lp_sts[l]) {
          rx_eq_lp_sts[l] = next_lp_sts;
          // log_link_bringup_event(link_cfg, EVENT_RX_EQ_LP_STATUS, l, rx_eq_lp_sts[l]);
        }
        if ((rx_eq_lp_sts[l] & 0x3F) == 0) {
          // need to complete toggle handshake for adapt req
          eth_pcs_ind_reg_rmw(0x1805c, 0, 12+l, 1);
          next_lane_rx_eq_state[l] = RX_EQ_WAIT_CLEAR_ADAPT_ACK;
        }
        break;
      }

      case RX_EQ_WAIT_CLEAR_ADAPT_ACK: {
        uint32_t next_rx_eq_ceu = eth_pcs_ind_reg_read(0x180c4+l);
        debug_buf[74+l] = next_rx_eq_ceu;
        if (((next_rx_eq_ceu >> 8) & 0x7) == 0) {
          next_lane_rx_eq_state[l] = RX_EQ_ISSUE_ADAPT_REQ;
        }
        break;
      }

      case RX_EQ_ISSUE_FINAL_ADAPT_REQ: {
        eth_pcs_ind_reg_rmw(0x1805c, 1, 12+l, 1);
        next_lane_rx_eq_state[l] = RX_EQ_WAIT_FINAL_PHY_UPDATE_CMD;
        break;
      }

      case RX_EQ_WAIT_FINAL_PHY_UPDATE_CMD: {
        // Software Polls for coefficient update command (given by local PHY) in VR_PMA_KRTR_RX_EQ_CEU Register
        uint32_t phy_ceu = eth_pcs_ind_reg_read(0x180c4+l);
        if ((phy_ceu >> 8) & 0x7) { // from the pcs logic, it seems all 3 bits are tied to the same lane adapt ack signal
          // need to complete toggle handshake for adapt req
          eth_pcs_ind_reg_rmw(0x1805c, 0, 12+l, 1);
          next_lane_rx_eq_state[l] = RX_EQ_WAIT_FINAL_CLEAR_ADAPT_ACK;
        }
        break;
      }

      case RX_EQ_WAIT_FINAL_CLEAR_ADAPT_ACK: {
        uint32_t next_rx_eq_ceu = eth_pcs_ind_reg_read(0x180c4+l);
        if (((next_rx_eq_ceu >> 8) & 0x7) == 0) {
          next_lane_rx_eq_state[l] = RX_EQ_DONE;
        }
        eth_pcs_ind_reg_write(0x180bc+l, (0x1 << 15) | (0x1 << 8)); // SET RR_RDY
        break;
      }

      case RX_EQ_DONE: {
        // reciever ready status is only read during RX_EQ_WAIT_UPDATE_STS or RX_EQ_WAIT_HOLD_STS.
        // once the RX_EQ state is past these points, the reciever ready status is not read anymore.
        // TX_EQ_DONE relies on receiver ready status to complete the transitions. Debug purposes only.
        uint32_t next_lp_sts = eth_pcs_ind_reg_read(l+0x104b0);
        if (next_lp_sts != rx_eq_lp_sts[l]) {
          rx_eq_lp_sts[l] = next_lp_sts;
        }
        break;
      }

      } // switch (curr_lane_rx_eq_state[l])

      if (curr_lane_tx_eq_state[l] != next_lane_tx_eq_state[l]) {
        // log_link_bringup_event(link_cfg, EVENT_TX_EQ_STATE_CHANGE, l, curr_lane_tx_eq_state[l], next_lane_tx_eq_state[l]);
        curr_lane_tx_eq_state[l] = next_lane_tx_eq_state[l];
      }
      if (curr_lane_rx_eq_state[l] != next_lane_rx_eq_state[l]) {
        // log_link_bringup_event(link_cfg, EVENT_RX_EQ_STATE_CHANGE, l, curr_lane_rx_eq_state[l], next_lane_rx_eq_state[l]);
        curr_lane_rx_eq_state[l] = next_lane_rx_eq_state[l];
      }

      debug_buf[78+l] = next_lane_tx_eq_state[l];
      debug_buf[82+l] = next_lane_rx_eq_state[l];
      debug_buf[86+l] = lane_training_frame_locked_iters[l];

    }
  } // while (!timeout)


  eth_link_state_t next_state;

  if (all_lanes_ok) {
    debug_buf[6] = all_lanes_status;
    log_link_bringup_event(link_cfg, EVENT_LANE_STATUS_UPDATE, all_lanes_status);
    next_state = LINK_AN_COMPLETE_WAIT;
  }
  else {
    debug_buf[6] = all_lanes_status;
    log_link_bringup_event(link_cfg, EVENT_LANE_ERR_AN_RESTART, all_lanes_status);
    next_state = LINK_AN_RESTART;
  }

  for (int l = 0; l < 4; l++) {

    uint32_t tx_asic_in1 = eth_phy_cr_read(0x1016 + (l << 8));
    uint32_t tx_asic_in2 = eth_phy_cr_read(0x1017 + (l << 8));
    uint32_t tx_pre = tx_asic_in2 & 0x3F; // pre
    uint32_t tx_post = (tx_asic_in2 >> 6) & 0x3F; // post
    uint32_t tx_main = (tx_asic_in1 >> 8) & 0x3F; // main
    debug_buf[22 + 3*l] = tx_pre;
    debug_buf[22 + 3*l + 1] = tx_post;
    debug_buf[22 + 3*l + 2] = tx_main;
  }

  // uint32_t fom_sts1 = eth_pcs_ind_reg_read(0x1809e); // VR_PMA_Gen5_12G_MISC_STS1
  // uint32_t fom_sts2 = eth_pcs_ind_reg_read(0x1809f); // VR_PMA_Gen5_12G_MISC_STS2

  // link_cfg->debug_buf[165] = fom_sts1 & 0xFF;
  // link_cfg->debug_buf[166] = fom_sts1 >> 8;
  // link_cfg->debug_buf[167] = fom_sts2 & 0xFF;
  // link_cfg->debug_buf[168] = fom_sts2 >> 8;

  return next_state;
}


eth_link_state_t link_training_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  eth_pcs_ind_reg_write(0x10096, 0x2);
  eth_pcs_ind_reg_write(0x10096, 0x3); // lt enable & restart

  log_link_bringup_event(link_cfg, EVENT_LINK_TRAINING_START);

  bool all_lanes_trained_found = false;

  // uint32_t ld_sts = 0;
  // uint32_t lp_sts = 0;

  while (true) {

    // uint32_t next_ld_sts = 0;
    // uint32_t next_lp_sts = 0;
    // for (uint32_t l = 0; l < 4; l++) {
    //   uint32_t lp_val = eth_pcs_ind_reg_read(l+0x104b0);
    //   next_lp_sts |= ((lp_val >> 15) << l);
    //   uint32_t ld_val = eth_pcs_ind_reg_read(l+0x10578);
    //   next_ld_sts |= ((ld_val >> 15) << l);
    // }
    // if (next_lp_sts != lp_sts) {
    //   lp_sts = next_lp_sts;
    //   log_link_bringup_event(link_cfg, EVENT_LP_LANES_TRAINED_STATUS, lp_sts);
    // }
    // if (next_ld_sts != ld_sts) {
    //   ld_sts = next_ld_sts;
    //   log_link_bringup_event(link_cfg, EVENT_LD_LANES_TRAINED_STATUS, ld_sts);
    // }

    uint32_t all_lanes_status = eth_pcs_ind_reg_read(0x10097);
    debug_buf[6] = all_lanes_status;
    bool all_lanes_ok = true;
    bool all_lanes_trained = true;
    bool bad_lane = false;
    for (uint32_t l = 0; l < 4; l++) {
      uint32_t lane_status = (all_lanes_status >> (4*l)) & 0xF;
      uint32_t fail = (lane_status >> 3) & 0x1;
      uint32_t trained = (lane_status >> 0) & 0x1;
      bool lane_ok = (lane_status == 0x1);
      all_lanes_ok &= lane_ok;
      all_lanes_trained &= trained;
      bad_lane |= fail;
    }

    if (all_lanes_trained && !all_lanes_trained_found) {
      debug_buf[6] = all_lanes_status;
      log_link_bringup_event(link_cfg, EVENT_ALL_LANES_TRAINED, all_lanes_status);
      all_lanes_trained_found = true;
    }

    if (bad_lane) {
      debug_buf[6] = all_lanes_status;
      log_link_bringup_event(link_cfg, EVENT_LANE_ERR_AN_RESTART, all_lanes_status);
      return LINK_AN_RESTART;
    }

    if (all_lanes_ok) {
      debug_buf[6] = all_lanes_status;
      log_link_bringup_event(link_cfg, EVENT_LINK_TRAINING_DONE_ALL_LANES_OK, all_lanes_status);
      return LINK_AN_COMPLETE_WAIT;
    }
  }
}


eth_link_state_t link_an_complete_wait_handler(volatile boot_params_t* link_cfg) {

  while (true) {
    uint32_t an_intr = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR
    if (an_intr == 0x1) { // an cmplt
      eth_pcs_ind_reg_write(0x78002, 0);
      log_link_bringup_event(link_cfg, EVENT_AN_INTR_COMPLETE);
      return LINK_PCS_ON_WAIT;
    } else if (an_intr != 0x0) {
      log_link_bringup_event(link_cfg, EVENT_AN_INTR_NOT_COMPLETE, an_intr);
      return LINK_AN_RESTART;
    }
  }
}


eth_link_state_t link_pcs_on_wait_handler(volatile boot_params_t* link_cfg) {
  uint32_t post_training_time_ms = 0;
  uint64_t curr_time = 0;
  uint64_t next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);

  // wait for continous adaptation and offset cancellation to settle
  if (!link_cfg->disable_cont_adapt || !link_cfg->disable_cont_off_can) {
    while (post_training_time_ms < 3000) {
      if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
        next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
        post_training_time_ms++;
      }
    }
  }

  post_training_time_ms = 0;
  next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);

  while (true) {
    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      post_training_time_ms++;
    }
    uint32_t val[3];
    uint32_t pcs_status = val[0] = eth_pcs_ind_reg_read(0x30001);
    uint32_t fec_status = val[1] = eth_pcs_ind_reg_read(0x100c9);
    // uint32_t an_interrupt = val[2] = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR
    val[2] = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR
    if ((pcs_status == 0x6) && (!link_cfg->rs_fec_en || (fec_status == 0xcf03))) {
      log_link_bringup_event(link_cfg, EVENT_PCS_LINK_ON, 3, val);
      return LINK_SYMERR_CHECK;
    }
    if (post_training_time_ms > link_cfg->pcs_on_wait_timeout_ms) {
      log_link_bringup_event(link_cfg, EVENT_LINK_BROKEN_RESTART, 3, val);
      return LINK_AN_RESTART;
    }
  }
}


eth_link_state_t link_symerr_check_handler(volatile boot_params_t* link_cfg, bool link_restart) {
  if (link_restart) {
    return LINK_RESTART_CHECK;
  }

  // Clear lane errs
  for (int l = 0; l < 4; l++) {
    eth_pcs_ind_reg_read(0x100d2+l*2);
    eth_pcs_ind_reg_read(0x100d3+l*2);
  }

  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  uint32_t post_training_time_ms = 0;
  uint64_t curr_time = 0;
  uint64_t next_1ms_time = 0;
  uint32_t val[3];

  log_link_bringup_event(link_cfg, EVENT_CRC_TEST_START);

  next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
  while (post_training_time_ms <= 1000) {

    bool tick_1ms = false;
    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      tick_1ms = true;
      post_training_time_ms++;
    }
    debug_buf[0] = 0xab000000 | post_training_time_ms;

    if (!tick_1ms) {
      // send traffic for crc checks, just keep transmitting the same data buf
      if (eth_txq_reg_read(1, ETH_TXQ_CMD) == 0) {
        eth_txq_reg_write(1, ETH_TXQ_CMD, ETH_TXQ_CMD_START_RAW);
      }
      continue;
    }

    bool symerr_check_failed = false;
    uint32_t lane_err[4];
    if (post_training_time_ms == 1000) {
      for (int l = 0; l < 4; l++) {
        uint32_t err_lo = eth_pcs_ind_reg_read(0x100d2+l*2);
        uint32_t err_hi = eth_pcs_ind_reg_read(0x100d3+l*2);
        uint32_t err = (err_hi << 16) + err_lo;
        debug_buf[7+l] = lane_err[l] = err;
        if (err > link_cfg->symerr_max) {
          symerr_check_failed = true;
        }
      }
    }

    uint32_t pcs_status = val[0] = eth_pcs_ind_reg_read(0x30001);
    uint32_t fec_status = val[1] = eth_pcs_ind_reg_read(0x100c9);
    uint32_t an_interrupt = val[2] = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR

    uint32_t crc_err[2];
    uint32_t crc_err_hi = crc_err[0] = eth_mac_reg_read(0x92c);
    uint32_t crc_err_lo = crc_err[1] = eth_mac_reg_read(0x928);
    uint64_t crc_errors = crc_err_hi;
    crc_errors = (crc_errors << 32);
    crc_errors += crc_err_lo;

    if (an_interrupt) {
      log_link_bringup_event(link_cfg, EVENT_AN_INT_LINK_RESTART, 3, val);
      return LINK_AN_RESTART;
    }
    if ((pcs_status != 0x6) || (link_cfg->rs_fec_en && (fec_status != 0xcf03))) {
      log_link_bringup_event(link_cfg, EVENT_LINK_BROKEN_RESTART, 3, val);
      return LINK_AN_RESTART;
    }
    if (symerr_check_failed) {
      log_link_bringup_event(link_cfg, EVENT_SYMERR_CHECK_FAILED, 4, lane_err);
      return LINK_AN_RESTART;
    }
    if (crc_errors > 0) {
      uint32_t restart_flags = 0x0;
      if (link_cfg->crc_err_tx_clear) {
        restart_flags |= 0x1;
      }
      if (link_cfg->crc_err_rx_clear) {
        restart_flags |= 0x2;
      }
      if (restart_flags != 0) {
        eth_pcs_ind_reg_rmw(0x38000, restart_flags, 8, 2); // reset pcs tx and/or rx path
        eth_pcs_ind_reg_poll(0x38000, 0x0, 0x300); // according to pcs databook, need to poll for these bits to go back to 0 before further operations
      }
      if (link_cfg->crc_err_link_restart) {
        log_link_bringup_event(link_cfg, EVENT_CRC_CHECK_FAILED, 2, crc_err);
        return LINK_AN_RESTART;
      } else {
        uint32_t mac_mmc_ctrl_val = eth_mac_reg_read(0x800);
        eth_mac_reg_write(0x800, mac_mmc_ctrl_val | 0x1); // reset all mac counters
      }
    }
  }

  log_link_bringup_event(link_cfg, EVENT_CRC_TEST_DONE);

  debug_buf[11] = eth_mac_reg_read(0x904); // MAC Frames count high
  debug_buf[12] = eth_mac_reg_read(0x900); // MAC Frames count low

  log_link_bringup_event(link_cfg, EVENT_SYMERR_CHECK_PASS, 3, val);
  return LINK_RESTART_CHECK;
}


eth_link_state_t link_packet_test_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;
  volatile uint32_t* rec_data_buf = (volatile uint32_t*)(TEMP_2K_BUF_ADDR + 1024);
  uint32_t my_eth_id = my_node_info->my_eth_id;

  // Synchronize the link power-up/reset sequence between two modules with dummy packet handshake
  if ((link_cfg->train_mode == TRAIN_STATIC) || (((link_cfg->port_enable_force >> my_eth_id) & 0x1) == 1)) {
    bool timeout = false;
    timeout = exchange_dummy_packet(link_cfg);
    // ensure both dummy packet and chip info is recieved
    if ((timeout) || (test_results[79] == 0)) {
      debug_buf[96] = LINK_INACTIVE_FAIL_DUMMY_PACKET;
      return LINK_NOT_ACTIVE;
    }
  }

  __attribute__((unused)) uint32_t tmp_val; // read misc. pcs error registers to clear
  tmp_val = eth_pcs_ind_reg_read(0x100ca);
  tmp_val = eth_pcs_ind_reg_read(0x100cb);
  tmp_val = eth_pcs_ind_reg_read(0x100cc);
  tmp_val = eth_pcs_ind_reg_read(0x100cd);
  for (int l = 0; l < 4; l++) {
    tmp_val = eth_pcs_ind_reg_read(0x100d2+l*2);
    tmp_val = eth_pcs_ind_reg_read(0x100d3+l*2);
    tmp_val = eth_pcs_ind_reg_read(0x38019);
  }
  tmp_val = eth_pcs_ind_reg_read(0x30021);
  tmp_val = eth_pcs_ind_reg_read(0x78002);
  tmp_val = eth_pcs_ind_reg_read(0x38020);

  uint32_t mac_mmc_ctrl_val = eth_mac_reg_read(0x800);
  eth_mac_reg_write(0x800, mac_mmc_ctrl_val | 0x1); // reset all mac counters

  uint64_t test_data_remaining = link_cfg->test_data_size_bytes;
  uint32_t round = 0;
  bool rx_check_fail = false;
  uint64_t data_comp_passed = 0;
  uint32_t round_skipped = 0;
  uint64_t t1 = eth_read_wall_clock();

  bool timeout = false;
  uint32_t read_val = 0;

  test_results[17] = eth_pcs_ind_reg_read(0x30001);
  // eth_ctrl_reg_write(ETH_CTRL_SCRATCH3, 0xA00A);
  while (test_data_remaining > 0) {
    uint32_t rx_rng = prng_next(0xabcd);
    round++;
    test_results[18] = round;
    uint32_t curr_data_size = (test_data_remaining < 1024) ? test_data_remaining : 1024;
    test_data_remaining -= curr_data_size;
    eth_send_packet(0, ((uint32_t)TEMP_2K_BUF_ADDR)>>4, ((uint32_t)(TEMP_2K_BUF_ADDR + 1024))>>4, curr_data_size >> 4);
    eth_send_packet(0, ((uint32_t)(&test_results[16]))>>4, ((uint32_t)(&test_results[20]))>>4, 1); //send round to otherside

    if (link_cfg->test_mode == TEST_MODE_PACKET_DATA) {
      uint64_t curr_time = 0;
      uint64_t next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
      uint32_t time_ms = 0;

      do {
        read_val = eth_pcs_ind_reg_read(0x30001);
        if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
          next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
          time_ms++;
        }
        timeout = ((time_ms > 1000) || (read_val != 0x6));
      } while((test_results[22] < round) && !timeout);

      if (test_results[22] > round) round_skipped++;
      if (timeout) {
        test_results[26] = round;
        rx_check_fail = true;
        // eth_ctrl_reg_write(ETH_CTRL_SCRATCH3, 0xB00A);
        break;
      }
      for (uint32_t i = 0; i < (curr_data_size/4); i++) {
        // rx_rng = 0xabcd0000 | i;
        rx_rng = prng_next(rx_rng);
        uint32_t rec_dw = rec_data_buf[i];
        if (rx_rng != rec_dw) {
          test_results[23] = rx_rng;
          test_results[24] = rec_dw;
          test_results[25] = i;
          test_results[26] = round;
          rx_check_fail = true;
          // eth_ctrl_reg_write(ETH_CTRL_SCRATCH3, 0xB00A);
          // break;
        } else {
          data_comp_passed++;
        }
      }
      // eth_ctrl_reg_write(ETH_CTRL_SCRATCH3, 0xC00A);
    }
  }

  test_results[27] = round_skipped;
  uint64_t t2 = eth_read_wall_clock();
  uint64_t t = t2 - t1;
  test_results[28] = t >> 32;
  test_results[29] = t & 0xFFFFFFFF;
  test_results[30] = rx_check_fail;
  data_comp_passed *= 4;
  test_results[31] = data_comp_passed >> 32;
  test_results[32] = data_comp_passed & 0xFFFFFFFF;

  for (int l = 0; l < 4; l++) {
    uint32_t err_lo = eth_pcs_ind_reg_read(0x100d2+l*2);
    uint32_t err_hi = eth_pcs_ind_reg_read(0x100d3+l*2);
    uint32_t err = (err_hi << 16) + err_lo;
    test_results[33+l] = err;
  }

  // test_results[37] = eth_pcs_ind_reg_read(0x30001); // pcs_status
  // test_results[38] = eth_pcs_ind_reg_read(0x100c9); // fec_status
  // test_results[39] = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR

  test_results[40] = eth_mac_reg_read(0x92c); // CRC HI
  test_results[41] = eth_mac_reg_read(0x928); // CRC LOW

  test_results[42] = eth_mac_reg_read(0x904); // mac frames high
  test_results[43] = eth_mac_reg_read(0x900); // mac frames low

  test_results[44] = eth_rxq_reg_read(0, ETH_RXQ_PACKET_DROP_CNT);

  // eth_ctrl_reg_write(ETH_CTRL_SCRATCH3, 0xD00A);

  uint32_t val[5];
  debug_buf[17] = val[0] = eth_pcs_ind_reg_read(0x78002);
  debug_buf[18] = val[1] = eth_pcs_ind_reg_read(0x30001);
  debug_buf[19] = val[2] = eth_pcs_ind_reg_read(0x100c9);
  debug_buf[20] = val[3] = eth_pcs_ind_reg_read(0x30032);
  debug_buf[21] = val[4] = eth_pcs_ind_reg_read(0x30008);

  if (timeout) {
    test_results[20] = 0;
    log_link_bringup_event(link_cfg, EVENT_LINK_TEST_MODE_TIMEOUT, 5, val);
    return LINK_AN_RESTART;
    // return LINK_NOT_ACTIVE;
  }

  log_link_bringup_event(link_cfg, EVENT_LINK_TEST_MODE_COMPLETED, 5, val);

  if (is_cable_loopback(link_cfg)) {
    debug_buf[96] = LINK_INACTIVE_CABLE_LOOPBACK;
    return LINK_NOT_ACTIVE;
  } else {
    return validate_chip_config(link_cfg);
  }
}


eth_link_state_t link_restart_check_handler(volatile boot_params_t* link_cfg, bool link_restart) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  volatile uint32_t* test_results = (volatile uint32_t*)RESULTS_BUF_ADDR;

  uint32_t handler_time_ms = 0;
  uint64_t curr_time = 0;
  uint64_t next_1ms_time = 0;

  uint32_t my_eth_id = my_node_info->my_eth_id;

  uint32_t val[3];

  // Enable tx/rx mac before link status check to allow packet exchange info
  if (link_restart) {
    eth_mac_reg_write(0x0, 0x30000001); // tx enable
    eth_mac_reg_write(0x4, 0x7); // rx enable + drop crc/pad
  }

  next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
  while (handler_time_ms <= link_cfg->restart_chk_time_ms) {

    bool tick_1ms = false;
    if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
      next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
      tick_1ms = true;
      handler_time_ms++;
    }

    debug_buf[0] = 0xac000000 | handler_time_ms;

    if (!tick_1ms) {
      continue;
    }

    // bool am_misaligned = false;
    // uint32_t reg = eth_ctrl_reg_read(ETH_CTRL_PCS_STATUS);
    // am_misaligned = (((reg >> 8) & 0x1) == 0x1);
    // if (am_misaligned) {
    //   eth_pcs_ind_reg_read(0x100ca);
    //   eth_ctrl_reg_write(ETH_CTRL_PCS_STATUS, 0x1 << 8);
    //   val[0] = reg;
    //   log_link_bringup_event(link_cfg, EVENT_AM_MISALIGNMENT, 1, val);
    //   return LINK_PCS_RESET;
    // }

    uint32_t pcs_status = val[0] = eth_pcs_ind_reg_read(0x30001); // rx link up, should get 0x6
    uint32_t fec_status = val[1] = eth_pcs_ind_reg_read(0x100c9);
    uint32_t an_interrupt = val[2] = eth_pcs_ind_reg_read(0x78002); // VR_AN_INTR

    if (an_interrupt) {
      log_link_bringup_event(link_cfg, EVENT_AN_INT_LINK_RESTART, 3, val);
      return LINK_AN_RESTART;
    }
    if ((pcs_status != 0x6) || (link_cfg->rs_fec_en && (fec_status != 0xcf03))) {
      log_link_bringup_event(link_cfg, EVENT_LINK_BROKEN_RESTART, 3, val);
      return LINK_AN_RESTART;
    }
    // Exit restart checking if recived RX dummy packet for LP
    if ((link_cfg->train_mode == TRAIN_STATIC) || (((link_cfg->port_enable_force >> my_eth_id) & 0x1) == 1)) {
      if (test_results[20] == 0xca11ab1e) break;
    // Exit restart checking if recieved remote chip info
    } else {
      if (test_results[79] == 1) break;
    }
  }

  // debug_buf[1] = equal to an attempts
  debug_buf[13] = eth_pcs_ind_reg_read(0x10097); // lane status
  debug_buf[14] = eth_pcs_ind_reg_read(0x30001); // rx link up, should get 0x6
  debug_buf[15] = eth_pcs_ind_reg_read(0x30008);
  debug_buf[16] = eth_pcs_ind_reg_read(0x38020);
  // debug_buf[8] = eth_pcs_ind_reg_read(0x78002);

  log_link_bringup_event(link_cfg, EVENT_LINK_RESTART_CHECK_PASS, 3, val);

  if (!link_restart) {
    enable_packet_mode(link_cfg);
    send_chip_info(link_cfg);
  }

  if (link_cfg->test_mode == TEST_MODE_PACKET_DATA || link_cfg->test_mode == TEST_MODE_PACKET_BANDWIDTH) return LINK_PACKET_TEST_MODE;

  // Restart check one more time to confirm no send packet timeouts
  if (!link_restart) {
    return LINK_RESTART_CHECK;
  } else {
    // Wait for synchronization between two modules with chip info exchange
    handler_time_ms = 0;
    next_1ms_time = eth_refclk_timestamp_init(REFCLK_1ms_COUNT);
    do {
      if (eth_refclk_timestamp_elapsed(&curr_time, &next_1ms_time)) {
        next_1ms_time = curr_time + (REFCLK_1ms_COUNT * clk_counter.refclk_scalar);
        handler_time_ms++;
      }
    } while ((test_results[79] == 0) && (handler_time_ms <= link_cfg->restart_chk_time_ms));
    if (test_results[79] == 0) {
      debug_buf[96] = LINK_INACTIVE_TIMEOUT_LINK_RESTART;
      return LINK_NOT_ACTIVE;
    }
  }

  if (is_cable_loopback(link_cfg)) {
    debug_buf[96] = LINK_INACTIVE_CABLE_LOOPBACK;
    return LINK_NOT_ACTIVE;
  } else {
    return validate_chip_config(link_cfg);
  }
}


eth_link_state_t link_pcs_reset_handler(volatile boot_params_t* link_cfg) {
  log_link_bringup_event(link_cfg, EVENT_PCS_RESET);

  uint32_t wait_cycles_1ms = 27000 * clk_counter.aiclk_scalar; //~1ms
  uint32_t wait_cycles_1us = 27 * clk_counter.aiclk_scalar; //~1us

  eth_mac_reg_write(0x0, 0x30000000); // tx disable
  eth_mac_reg_write(0x4, 0x6); // rx disable

  eth_wait_cycles(100 * wait_cycles_1us);  // wait for 100us

  // clear pcs misalignment counter from PCS and Eth Ctrl before restarting the link
  eth_pcs_ind_reg_read(0x100ca);
  eth_pcs_ind_reg_read(0x100cb);
  eth_ctrl_reg_write(ETH_CTRL_PCS_STATUS, 0x1 << 8);
  eth_ctrl_reg_write(ETH_CTRL_ETH_RESET, ETH_SOFT_RESETN | ETH_MAC_SOFT_PWR_ON_RESETN | ETH_MAC_SOFT_RESETN);
  eth_ctrl_reg_write(ETH_CTRL_PCS_STATUS, 0x1 << 8);
  eth_wait_cycles(wait_cycles_1ms);

  return LINK_POWERUP;
}


void link_pcs_active_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  uint32_t val[5];
  debug_buf[17] = val[0] = eth_pcs_ind_reg_read(0x78002);
  debug_buf[18] = val[1] = eth_pcs_ind_reg_read(0x30001);
  debug_buf[19] = val[2] = eth_pcs_ind_reg_read(0x100c9);
  debug_buf[20] = val[3] = eth_pcs_ind_reg_read(0x30032);
  debug_buf[21] = val[4] = eth_pcs_ind_reg_read(0x30008);
  log_link_bringup_event(link_cfg, EVENT_LINK_ACTIVE, 5, val);
  debug_buf[99] = 0xfacafaca;
}


void link_pcs_inactive_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  log_link_bringup_event(link_cfg, EVENT_LINK_NOT_ACTIVE);
  // if (link_cfg -> link_inactive_pwr_off) {
  //   log_link_bringup_event(link_cfg, EVENT_LINK_PWR_OFF);
  //   eth_reset_assert();
  // }
  debug_buf[99] = 0xfacafaca;
}


void link_test_mode_handler(volatile boot_params_t* link_cfg) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;
  debug_buf[1] = 0xbeef;
  eth_pcs_ind_reg_rmw(0x3002a, 0x1, 4, 1); // PRBS31_T_EN
  eth_pcs_ind_reg_rmw(0x105dd, 0x1, 7, 1); // PRBS31
  eth_pcs_ind_reg_rmw(0x105dd, 0x1, 3, 1); // enable PRBS transmit in PMA layer
  eth_pcs_ind_reg_rmw(0x3002a, 0x1, 5, 1); // PRBS31_R_EN
  eth_pcs_ind_reg_rmw(0x105dd, 0x1, 0, 1); // Enable test pattern checker in PMA layer

  vendor_specific_reset_pcs(clk_counter.aiclk_scalar, link_cfg);

  uint32_t val[5];
  debug_buf[17] = val[0] = eth_pcs_ind_reg_read(0x78002);
  debug_buf[18] = val[1] = eth_pcs_ind_reg_read(0x30001);
  debug_buf[19] = val[2] = eth_pcs_ind_reg_read(0x100c9);
  debug_buf[20] = val[3] = eth_pcs_ind_reg_read(0x30032);
  debug_buf[21] = val[4] = eth_pcs_ind_reg_read(0x30008);
  log_link_bringup_event(link_cfg, EVENT_LINK_TEST_MODE_COMPLETED, 5, val);
  debug_buf[99] = 0xfacafaca;
}


link_train_status_e eth_link_start_handler(volatile boot_params_t* link_cfg, eth_link_state_t start_link_state, bool link_restart) {
  volatile uint32_t* debug_buf = (volatile uint32_t*)DEBUG_BUF_ADDR;

  eth_link_state_t link_state = start_link_state;
  pg_rcv_fail = 0;
  an_attempts = 0;

  debug_buf[98] = (ETH_TRAIN_VERSION_MAJOR << 16) | (ETH_TRAIN_VERSION_MINOR << 8) | ETH_TRAIN_VERSION_TEST;

  while (true) {
    debug_buf[0] = 0xabcd0000 | link_state;
    switch (link_state) {
    case LINK_ACTIVE:
      link_pcs_active_handler(link_cfg);
      return LINK_TRAIN_SUCCESS;
    case LINK_POWERUP:
      link_state = link_powerup_handler(link_cfg, link_restart);
      break;
    case LINK_AN_CFG:
      link_state = link_an_cfg_handler(link_cfg);
      break;
    case LINK_AN_RESTART:
      link_state = link_an_restart_handler(link_cfg);
      break;
    case LINK_AN_PG_RCV:
      link_state = link_an_pg_rcv_handler(link_cfg);
      break;
    case LINK_TRAINING:
      link_state = link_training_handler(link_cfg);
      break;
    case LINK_TRAINING_FW:
      link_state = link_training_fw_handler(link_cfg);
      break;
    case LINK_AN_COMPLETE_WAIT:
      link_state = link_an_complete_wait_handler(link_cfg);
      break;
    case LINK_PCS_ON_WAIT:
      link_state = link_pcs_on_wait_handler(link_cfg);
      break;
    case LINK_SYMERR_CHECK:
      link_state = link_symerr_check_handler(link_cfg, link_restart);
      break;
    case LINK_RESTART_CHECK:
      link_state = link_restart_check_handler(link_cfg, link_restart);
      if (link_state == LINK_RESTART_CHECK) {
        link_restart = true;
      }
      break;
    case LINK_NO_AN_START:
      link_state = link_no_an_start_handler(link_cfg);
      break;
    case LINK_PCS_RESET:
      link_state = link_pcs_reset_handler(link_cfg);
      break;
    case LINK_NOT_ACTIVE:
      link_pcs_inactive_handler(link_cfg);
      return LINK_TRAIN_TIMEOUT;
    case LINK_TEST_MODE:
      link_test_mode_handler(link_cfg);
      return LINK_TRAIN_TEST_MODE;
    case LINK_PACKET_TEST_MODE:
      link_state = link_packet_test_handler(link_cfg);
      if (link_state == LINK_AN_RESTART) {
        link_restart = true;
      }
      break;
    // case LINK_CRC_CHECK_MAC_MEM_DUMP:
    //   link_state = link_crc_check_mac_mem_dump_handler(link_cfg);
    //   break;
    default:
      debug_buf[0] = 0xFFFFFFFF;
      while(true);
      break;
    }
  }
}
