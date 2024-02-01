// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef ETH_INIT_H
#define ETH_INIT_H

#include <stdint.h>

#include "eth_ss_regs.h"

#ifdef TB_NOC

extern "C" {
#include "noc_api_dpi.h"
}
#include "tb_noc_dpi.h"


#define ETH_LOG(format, ...)                                            \
  {                                                                     \
    LOGT("[chip_id=%d, x=%d, y=%d]: ", dpi_get_chip_id(), dpi_get_x(), dpi_get_y()); \
    LOG((format), __VA_ARGS__);                                         \
  }


#else

#ifdef ETH_INIT_FW
   #define ETH_WRITE_REG(addr, val) ((*((volatile uint32_t*)((addr)))) = (val))
   #define ETH_READ_REG(addr)       (*((volatile uint32_t*)((addr))))
#else
   #define ETH_WRITE_REG(addr, val) ((*((volatile uint32_t*)(noc_get_cmd_buf()*NOC_CMD_BUF_OFFSET+noc_get_active_instance()*NOC_INSTANCE_OFFSET+(addr)))) = (val))
   #define ETH_READ_REG(addr)       (*((volatile uint32_t*)(noc_get_cmd_buf()*NOC_CMD_BUF_OFFSET+noc_get_active_instance()*NOC_INSTANCE_OFFSET+(addr))))
#endif

inline void ETH_LOG(const char* format, ...) {} // make log no-op in firmware

inline void LOGT(const char* format, ...) {} // make log no-op in firmware

// IMPROVE imatosevic - add firmware implementation of clk_tick here
void clk_tick(uint32_t count);

#endif

#define EPOCH_MAX_DIFF 15

// For training internal use only
#define ETH_TRAIN_VERSION_MAJOR  0x6
#define ETH_TRAIN_VERSION_MINOR  0x0
#define ETH_TRAIN_VERSION_TEST   0x0

#define ETH_PHY_FW_BYTE_SIZE (32*1024)

// RAWMEM_DIG_RAM_CMN0_B0_R0 = 0xc000
#define ETH_PHY_SRAM_OVRD_START_ADDR (0xc000)

// boot refclk = 27MHz
#define BOOT_REFCLK_PERIOD_PS (37000)

// 32 chips per galaxy board
#define CHIP_COORD_MASK 0xFF
#define CHIP_COORD_X_SHIFT 0
#define CHIP_COORD_Y_SHIFT 8

#define TX_EQ_MASK 0x3F

#define TX_EQ_MAIN_SHIFT 0
#define TX_EQ_PRE_SHIFT  8
#define TX_EQ_POST_SHIFT 16

#define CTLE_BOOST_MASK 0x1F
#define CTLE_POLE_MASK 0x3
#define CTLE_VGA1_GAIN_MASK 0x7
#define CTLE_VGA2_GAIN_MASK 0x7

#define CTLE_BOOST_SHIFT 0
#define CTLE_POLE_SHIFT  8
#define CTLE_VGA1_GAIN_SHIFT 12
#define CTLE_VGA2_GAIN_SHIFT 16

#define BOOT_PARAMS_ADDR (0x1000)
#define BOOT_PARAMS_SIZE (256)

#define NODE_INFO_ADDR (BOOT_PARAMS_ADDR+BOOT_PARAMS_SIZE)
#define NODE_INFO_SIZE (256)

#define ETH_CONN_INFO_ADDR (NODE_INFO_ADDR+NODE_INFO_SIZE)
#define ETH_CONN_INFO_SIZE (48*4)

#define DEBUG_BUF_ADDR (ETH_CONN_INFO_ADDR+ETH_CONN_INFO_SIZE)
#define DEBUG_BUF_SIZE (1024)

#define TEMP_2K_BUF_ADDR (DEBUG_BUF_ADDR+DEBUG_BUF_SIZE)
#define TEMP_2K_BUF_SIZE (2048)

#define RESULTS_BUF_ADDR (TEMP_2K_BUF_ADDR+TEMP_2K_BUF_SIZE)
#define RESULTS_BUF_SIZE (320)

// ARC refclk counter addresses
#define ARC_REFCLK_ADDR_LO 0x20
#define ARC_REFCLK_ADDR_HI 0x24

#define REFCLK_1ms_COUNT 27000

// The upp 16 MSB of port_enable_force is used to denote whether the credos are connected in loopback mode
// if set, the link should be seen as not active to enable routing in the case of loopback testing
#define ENABLE_CREDO_LOOPBACK_SHIFT 16

typedef enum {LOOPBACK_OFF=0, LOOPBACK_MAC=1, LOOPBACK_PHY=2, LOOPBACK_PCS=3, LOOPBACK_PHY_EXTERN=4} eth_loopback_mode_e;
typedef enum {TRAIN_HW_AUTO=0, TRAIN_STATIC=1, TRAIN_FW=2} link_train_mode_e;
typedef enum {TEST_MODE_OFF=0, TEST_MODE_PHY_PRBS=1, TEST_MODE_PACKET_DATA=2, TEST_MODE_PACKET_BANDWIDTH=3} link_test_mode_e;

typedef enum {GALAXY=0, NEBULA_X1=1, NEBULA_X2_LEFT=2, NEBULA_X2_RIGHT=3} board_type_e;

typedef enum {
  LINK_TRAIN_FAIL = 0,
  LINK_TRAIN_SUCCESS = 1,
  LINK_TRAIN_TIMEOUT = 2,
  LINK_TRAIN_TEST_MODE = 3,
} link_train_status_e;

typedef enum {
  LINK_INACTIVE_STATUS_NONE = 0,
  LINK_INACTIVE_ERROR_CHIP_ID = 1,
  LINK_INACTIVE_ERROR_RACK_ID = 2,
  LINK_INACTIVE_ERROR_SHELF_ID = 3,
  LINK_INACTIVE_ERROR_BOARD_ID = 4,
  LINK_INACTIVE_TIMEOUT_LINK_RESTART = 5,
  LINK_INACTIVE_MAC_LOOPBACK = 6,
  LINK_INACTIVE_PHY_LOOPBACK = 7,
  LINK_INACTIVE_CABLE_LOOPBACK = 8,
  LINK_INACTIVE_TIMEOUT_AN_ATTEMPTS = 9,
  LINK_INACTIVE_FAIL_DUMMY_PACKET = 10,
  LINK_INACTIVE_TIMEOUT_SIGDET = 11,
  LINK_INACTIVE_TIMEOUT_PG_RCV = 12,
  LINK_INACTIVE_PORT_NOT_POPULATED = 13,
  LINK_INACTIVE_PORT_MASKED_OFF = 14,
  LINK_INACTIVE_STATUS_COUNT = 15,
} link_inactive_status_e;

typedef enum {
 LINK_POWERUP = 0,
 LINK_AN_RESTART = 1,
 LINK_AN_CFG = 2,
 LINK_AN_PG_RCV = 3,
 LINK_TRAINING = 4,
 LINK_AN_COMPLETE_WAIT = 5,
 LINK_PCS_ON_WAIT = 6,
 LINK_SYMERR_CHECK = 7,
 LINK_RESTART_CHECK = 8,
 LINK_NO_AN_START = 9,
 LINK_ACTIVE = 10,
 LINK_PCS_RESET = 11,
 LINK_CRC_CHECK_MAC_MEM_DUMP = 12,
 LINK_TRAINING_FW = 13,
 LINK_NOT_ACTIVE = 14,
 LINK_TEST_MODE = 15,
 LINK_PACKET_TEST_MODE = 16,
 LINK_RESERVED = 17
} eth_link_state_t;

typedef struct {
  uint8_t refclk_valid;
  uint8_t refclk_timeout;
  uint8_t refclk_scalar;
  uint8_t aiclk_scalar;
  uint64_t prev_refclk_count;
  uint32_t refclk_invalid_counter;
} clk_counter_t;

// Max 64 DWs
typedef struct {
  uint32_t local_chip_coord;
  uint32_t suppress_los_det;
  uint32_t port_disable_mask; // use for custom galaxy topologies
  uint32_t rs_fec_en;
  uint32_t am_timer_override;

  uint32_t tx_store_and_forward;
  uint32_t loopback_mode;
  uint32_t train_mode;
  uint32_t disable_cont_adapt;
  uint32_t disable_cont_off_can;

  uint32_t prbs_en;
  uint32_t crc_err_tx_clear;
  uint32_t crc_err_rx_clear;
  uint32_t crc_err_link_restart;
  uint32_t timeout_an_attemps;

  uint32_t prbs_max_errors;
  uint32_t test_data_size_bytes;
  uint32_t symerr_max;
  uint32_t aiclk_ps;
  uint32_t spare2;

  uint32_t spare3;
  uint32_t am_timer_val;
  uint32_t local_mac_addr_high;
  uint32_t local_mac_addr_low;
  uint32_t bcast_mac_addr_high;

  uint32_t bcast_mac_addr_low;
  uint32_t local_seq_update_timeout;
  uint32_t remote_seq_timeout;
  uint32_t max_packet_size_bytes;
  uint32_t min_packet_size_words;

  uint32_t burst_len;
  uint32_t link_bringup_log_enable;
  uint32_t an_attempts_before_pcs_reset;
  uint32_t lt_const_amp_alg;
  uint32_t pg_rcv_fail_limit;

  uint32_t pcs_on_wait_timeout_ms;
  uint32_t restart_chk_time_ms;
  uint32_t pg_rcv_wait_timeout_ms;
  uint32_t ping_pong_alg_mode;
  uint32_t override_tx_eq_lane0;
  uint32_t override_tx_eq_lane1;
  uint32_t override_tx_eq_lane2;
  uint32_t override_tx_eq_lane3;

  uint32_t test_mode;
  uint32_t tx_eq_preset_en; // Default use initialize
  uint32_t rx_eq_preset_en; // Default use initialize
  uint32_t link_status_check_en;
  uint32_t mac_rcv_fail_limit;

  uint32_t override_ctle_lane0;
  uint32_t override_ctle_lane1;
  uint32_t override_ctle_lane2;
  uint32_t override_ctle_lane3;
  uint32_t port_enable_force; // Bit mask, for static seq only
  uint32_t shelf_rack_coord; // shelf and rack coordinates. [7:0] = rack, [15:8] = shelf

  uint32_t link_training_fw_phony; //debug feature
  uint32_t dram_trained;
  uint32_t board_type;
  uint32_t chip_version; // A0 or B0
  uint32_t run_test_app;

  uint32_t board_id;
  uint32_t validate_chip_config_en;
  uint32_t spare[3];
} boot_params_t;

inline uint32_t prng_next(uint32_t seed) {
  uint32_t result = seed;
  result = result ^ (result << 13);
  result = result ^ (result >> 17);
  result = result ^ (result << 5);
  return result;
}

uint64_t eth_read_wall_clock();

void eth_wait_cycles(uint32_t wait_cycles);

void refclk_counter_init();

void eth_reset_deassert();

void eth_reset_assert();

void eth_risc_reg_write(uint32_t offset, uint32_t val);

uint32_t eth_risc_reg_read(uint32_t offset);

void eth_mac_reg_write(uint32_t offset, uint32_t val);

uint32_t eth_mac_reg_read(uint32_t offset);

void eth_pcs_dir_reg_write(uint8_t reg_id, uint16_t val);

uint16_t eth_pcs_dir_reg_read(uint8_t reg_id);

void eth_pcs_ind_reg_write(uint32_t offset, uint16_t val);

void eth_pcs_ind_reg_rmw(uint32_t offset, uint16_t val, uint32_t lsb, uint32_t num_bits);

uint16_t eth_pcs_ind_reg_read(uint32_t offset);

void eth_ctrl_reg_write(uint32_t offset, uint32_t val);

uint32_t eth_ctrl_reg_read(uint32_t offset);

void eth_txq_reg_write(uint32_t qnum, uint32_t offset, uint32_t val);

uint32_t eth_txq_reg_read(uint32_t qnum, uint32_t offset);

void eth_rxq_reg_write(uint32_t qnum, uint32_t offset, uint32_t val);

uint32_t eth_rxq_reg_read(uint32_t qnum, uint32_t offset);

void eth_pcs_ind_reg_poll(uint32_t reg_id, uint16_t value, uint16_t reg_mask = 0xFFFF, uint32_t poll_wait = 0, volatile uint32_t* dbg_val_ptr = 0);

void eth_mac_init(volatile boot_params_t* link_cfg);

void eth_rxq_init(uint32_t q_num, uint32_t rec_buf_word_addr, uint32_t rec_buf_size_words, bool rec_buf_wrap,  bool packet_mode);

uint16_t eth_phy_cr_read(uint16_t cr_addr);

void eth_phy_cr_write(uint16_t cr_addr, uint16_t cr_data);

void eth_write_remote_reg(uint32_t q_num, uint32_t reg_addr, uint32_t val);

void eth_epoch_sync(uint32_t epoch_id, bool has_eth_stream_trans, volatile uint32_t &epoch_id_to_send, volatile uint32_t &other_chip_epoch_id, void (*risc_context_switch)());

void eth_pcs_set_cr_para_sel(uint32_t val);

link_train_status_e eth_link_start_handler(volatile boot_params_t* link_cfg, eth_link_state_t start_link_state, bool link_restart);

bool exchange_dummy_packet(volatile boot_params_t* link_cfg);

#endif

