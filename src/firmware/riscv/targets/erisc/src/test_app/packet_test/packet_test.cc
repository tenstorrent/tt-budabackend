
#include "eth_init.h"
#include "overlay.h"
#include "noc.h"
#include "risc.h"
#include "noc_nonblocking_api.h"
#include "eth_routing_v2.h"
#include "eth_ss.h"
#include "test_app_common.h"

#define ETH_APP_VERSION_MAJOR  0x1
#define ETH_APP_VERSION_MINOR  0x3
#define ETH_APP_VERSION_TEST   0x0

volatile uint32_t* const eth2eth_test_log_buf    = (volatile uint32_t*)(1024 * (3*32));
volatile uint32_t* const eth2eth_tx_data_buf     = (volatile uint32_t*)(1024 * (5*32));
volatile uint32_t* const eth2eth_rx_data_buf_0   = (volatile uint32_t*)(1024 * (6*32));
volatile uint32_t* const eth2eth_rx_data_buf_1   = (volatile uint32_t*)(1024 * (7*32));

const uint32_t eth_l1_size_bytes = 256 * 1024;
const uint32_t eth2eth_test_log_buf_size_dws = (16 * 1024)/4;

const uint32_t test_args_size_bytes = (1024 * 1);
const uint32_t test_results_size_bytes = (1024 * 1);
const uint32_t test_app_debug_size_bytes = (1024 * 1);

const uint32_t WORD_SIZE_BYTES = 16;


const uint32_t TEST_END_ETH_ACTIVE = 1;
const uint32_t TEST_END_LOOPBACK = 2;
const uint32_t TEST_END_TILE_HEADER_CHECK_FAIL = 3;
const uint32_t TEST_END_DATA_CHECK_FAIL = 4;
const uint32_t TEST_END_ETH_NOT_ACTIVE = 5;
const uint32_t TEST_END_TENSIX_NOT_ACTIVE = 6;
const uint32_t TEST_END_TENSIX_ACTIVE = 6;
const uint32_t TEST_END_REACHED_MAIN_END = 7;
const uint32_t TEST_END_REMOTE_ERR_FAIL = 8;
const uint32_t TEST_END_MEASURE_INITIAL_BER = 9;
const uint32_t TEST_END_CRC_ERR_MAC_MEM_DUMP = 10;

static uint32_t my_eth_x;
static uint32_t my_eth_y;
static uint32_t test_rnd_seed;
static uint32_t test_inject_error;
static uint32_t test_inject_error_tx_eth;
static uint32_t test_inject_error_iter;
static uint32_t test_inject_error_bit;

static uint32_t this_is_eth0;
static uint32_t this_is_eth1;

static bool test_dry_run;

static uint32_t eth0_id; 
static uint32_t eth1_id;
static uint32_t eth0_x;
static uint32_t eth0_y;
static uint32_t eth1_x;
static uint32_t eth1_y;
static uint32_t eth2eth_tx_data_buf_size;

static uint32_t test_num_iters;

static bool parallel_loopback_mode;

static bool packet_mode;

static bool parallel_cable_mode;
static uint32_t parallel_cable_enable_mask;

static uint32_t board_type;
static uint32_t test_remote_write;

const uint32_t OUTSTANDING_READS = 4;
static uint32_t noc_rd_buf[OUTSTANDING_READS*8] __attribute__((aligned(32))) ;
static uint32_t noc_rd_issued[2];

uint32_t noc_reads_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_acked[NUM_NOCS];

////


#define NOC_SCRATCH_REG 0xFFB2010C

// Differences between A0 and B0
#define WH_B0_ID 0x30424857
#define WH_A0_CODE 0xa
#define WH_B0_CODE 0xb

static uint32_t chip_version;

static uint32_t eth2eth_test_log_buf_index = 0;

inline uint32_t fast_prng_check(volatile uint32_t* start_addr, uint32_t num_16B_words, uint32_t seed, uint32_t iter, uint32_t tile_index) {

  uint32_t num_errors = 0;
  
  uint32_t rd0;
  uint32_t rd1;
  uint32_t rd2;
  uint32_t rd3;
  
  uint32_t exp0;
  uint32_t exp1;
  uint32_t exp2;
  uint32_t exp3;

  uint32_t fail = 0;
  volatile uint32_t* src = start_addr;
  volatile uint32_t* src_end = start_addr + (num_16B_words * 4);

  exp3 = seed;
  rd0 = src[0];
  rd1 = src[1];
  rd2 = src[2];
  rd3 = src[3];
  while (src != src_end) {
    exp0 = prng_next(exp3);
    exp1 = prng_next(exp0);
    exp2 = prng_next(exp1);
    exp3 = prng_next(exp2);
    fail =
      (rd0 ^ exp0) |
      (rd1 ^ exp1) |
      (rd2 ^ exp2) |
      (rd3 ^ exp3);
    if (fail != 0) {
      num_errors++;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+0] = 1;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+1] = (uint32_t)start_addr;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+2] = iter;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+3] = tile_index;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+4] = (uint32_t)src;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+8] = rd0;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+9] = rd1;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+10] = rd2;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+11] = rd3;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+12] = exp0;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+13] = exp1;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+14] = exp2;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+15] = exp3;
      eth2eth_test_log_buf_index += 16;
      if (eth2eth_test_log_buf_index == (eth2eth_test_log_buf_size_dws)) {
        eth2eth_test_log_buf_index = 0;
      }
    } else {
      src[0] = 0;
      src[1] = 0;
      src[2] = 0;
      src[3] = 0;
    }
    src += 4;
    rd0 = src[0];
    rd1 = src[1];
    rd2 = src[2];
    rd3 = src[3];
  }

  return num_errors;
}

////

const uint32_t POLL_WAIT_CYCLES = 20;

void finish_test(uint32_t status_val, uint32_t fail) {
  while (test_results[0] != 0x22222222) {
    risc_context_switch();
  };
  test_results[0] = 0xabcdabcd;
  test_results[1] = fail;
  test_results[2] = status_val;
  while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
  uint64_t read_addr = NOC_XY_ADDR(my_eth_x, my_eth_y, (uint32_t)&test_app_debug[2]); // Pretty much a dummy value, unused
  uint64_t wr_ptr_addr = NOC_XY_ADDR(eth0_x, eth0_y, (uint32_t)(&(test_results[3])));
  noc_atomic_read_and_increment (0, NCRISC_SMALL_TXN_CMD_BUF, wr_ptr_addr, 1, 31, read_addr, false, 0);
  config_buf->run_test_app = 0;
}


void test_remote_reg_writes_simple(void) {
  while (test_results[0] != 0x22222222) {
    risc_context_switch();
  };
  uint32_t local_sum = eth_ctrl_reg_read(ETH_CTRL_TEST_SUM);
  uint32_t round = 0;
  uint32_t tx_rng = test_rnd_seed;
  uint32_t num_rounds = 1000;
  test_results[34] = test_rnd_seed;
  test_results[35] = eth_ctrl_reg_read(ETH_CTRL_TEST_SUM);

  uint64_t t1 = eth_read_wall_clock();
  while(round < num_rounds) {
    round++;
    local_sum += round;
    eth_write_remote_reg(0, ETH_CTRL_REGS_START+ETH_CTRL_TEST_SUM, round);
    uint32_t pcs_status = eth_pcs_ind_reg_read(0x30001);
    test_results[36] = round;
    test_results[37] = local_sum;
  }

  eth_ctrl_reg_write(ETH_CTRL_SCRATCH1, 0x300B);
  uint64_t t2 = eth_read_wall_clock();
  uint64_t t = t2 - t1;
  test_results[38] = t & 0xFFFFFFFF;
  test_results[39] = t >> 32;
  test_results[40] = local_sum;
  test_results[41] = eth_ctrl_reg_read(ETH_CTRL_TEST_SUM);
  test_results[146] = round;
  eth_write_remote_reg(0, ETH_CTRL_REGS_START+ETH_CTRL_SCRATCH2, 0xaa0000 | round);
  eth_send_packet(0, ((uint32_t)(test_results + 144))>>4, ((uint32_t)(test_results + 140))>>4, 1); //send round to otherside
  while(test_results[142] != round);
 
  if (local_sum != eth_ctrl_reg_read(ETH_CTRL_TEST_SUM)) {
    test_results[42] = 1;
  } else {
    test_results[42] = 0;
  }
}

void test_traffic_packet_eth(uint32_t eth01) {
  // Read clear errors just in case
  for (int i = 0; i < 4; i++) {
    eth_pcs_ind_reg_read(0x100d2+i*2);
    eth_pcs_ind_reg_read(0x100d3+i*2);
  }
  eth_pcs_ind_reg_read(0x30021);

  uint64_t t_eth2eth_tx_start;
  uint64_t t_eth2eth_tx_end;

  test_results[5] = 0xabcd0000 | eth01;

  eth2eth_test_log_buf_index = 0;

  bool first_eth2eth_tx_iter = true;

  uint32_t test_log_index = 0;

  // Make both ethernets start at approximately the same time
  while (test_results[0] != 0x22222222) {
    test_app_debug[0] = 0xcdef0000;
    risc_context_switch();
  }

  uint64_t data_start_time = eth_read_wall_clock();
  uint32_t words_received = eth_rxq_reg_read(0, ETH_RXQ_WORD_CNT);

  uint32_t total_16B_words = eth2eth_tx_data_buf_size / 16;

  volatile uint32_t* data_buf_ptr = eth2eth_tx_data_buf;
  uint32_t tile_seed = 0;
  uint32_t tile_index = 127;

  uint32_t fail = 0;
  uint32_t errors_found = 0;
  uint32_t status_val = 0;
  uint64_t bytes_checked = 0;
  uint64_t initial_corr_cw = ((uint64_t)results_buf[52] << 32) | (uint64_t)results_buf[53];
  uint64_t initial_uncorr_cw = ((uint64_t)results_buf[54] << 32) | (uint64_t)results_buf[55];
  
  uint64_t mac_addr = config_buf->bcast_mac_addr_high;
  mac_addr = (mac_addr << 32) | config_buf->bcast_mac_addr_low;

  tile_seed = get_tile_rnd_seed(test_rnd_seed, eth01, 0, tile_index);
  test_results[41] = tile_index;
  test_results[42] = tile_seed;

  test_app_debug[0] = 0xA0000001;
  data_buf_ptr[0] = total_16B_words;
  data_buf_ptr[1] = tile_index;
  data_buf_ptr[2] = eth01;
  data_buf_ptr[3] = tile_seed;
  fast_prng_init(data_buf_ptr+4, total_16B_words-1, tile_seed);
    
  if (test_inject_error) {
    if ((test_inject_error_tx_eth == eth01) && (test_inject_error_iter == 0)) {            
      uint32_t val = data_buf_ptr[test_inject_error_bit];
      uint32_t mask = (0x1 << (test_inject_error_bit%32));
      data_buf_ptr[test_inject_error_bit] = val ^ mask;
    }
  }

  test_app_debug[0] = 0xA0000002;

  bool early_exit = false;

  for (uint32_t iter = 1; iter < (test_num_iters + 1) && config_buf->run_test_app; iter++) {
    test_app_debug[0] = 0xcdef0001;

    uint64_t t_tx_start = eth_read_wall_clock();

    test_results[8] = t_tx_start >> 32;
    test_results[9] = t_tx_start & 0xFFFFFFFF;
    test_results[10] = packet_mode;

    volatile uint32_t* dest_addr = 0;
    test_results[24] = iter;
    if (iter % 2) {
      dest_addr = eth2eth_rx_data_buf_0; 
      test_results[25] = iter;
    } else {
      dest_addr = eth2eth_rx_data_buf_1;
      test_results[26] = iter;
    }

    if (first_eth2eth_tx_iter) {
      t_eth2eth_tx_start = eth_read_wall_clock();
      first_eth2eth_tx_iter = false;
    }

    if (packet_mode) {
      eth_send_packet(0, ((uint32_t)(eth2eth_tx_data_buf))>>4, ((uint32_t)(dest_addr))>>4, (eth2eth_tx_data_buf_size/16));
      eth_send_packet(0, ((uint32_t)(test_results + 24))>>4, ((uint32_t)(test_results + 28))>>4, 1);
      uint32_t rcv_iter = (iter%2 ? test_results[29]:test_results[30]);
      while(rcv_iter != iter && !early_exit) {
        risc_context_switch();
        rcv_iter = (iter%2 ? test_results[29]:test_results[30]);
        early_exit = !config_buf->run_test_app;
      };
    } else {
      eth_send_raw(0, (uint32_t)(eth2eth_tx_data_buf), eth2eth_tx_data_buf_size, mac_addr);
      bool rx_done = false;
      while (!rx_done && !early_exit) {
        uint32_t rx_buf_ptr = eth_rxq_reg_read(0, ETH_RXQ_BUF_PTR);
        test_results[28] = rx_buf_ptr;
        uint32_t mem_wr_pending = eth_rxq_reg_read(0, ETH_RXQ_OUTSTANDING_WR_CNT);
        rx_done = (rx_buf_ptr == eth2eth_tx_data_buf_size) && (mem_wr_pending == 0);
        risc_context_switch();
        early_exit = !config_buf->run_test_app;
      }
      eth_rxq_reg_write(0, ETH_RXQ_BUF_PTR, 0);
      if (iter % 2) {
        eth_rxq_reg_write(0, ETH_RXQ_BUF_START_WORD_ADDR, ((uint32_t)(eth2eth_rx_data_buf_1)>>4));
      } else {
        eth_rxq_reg_write(0, ETH_RXQ_BUF_START_WORD_ADDR, ((uint32_t)(eth2eth_rx_data_buf_0)>>4));
      }
    }
    
    test_app_debug[0] = 0xcdef0002;

    uint64_t t_tx_done = eth_read_wall_clock();
    uint64_t t_tx = t_tx_done - t_tx_start;
    test_results[11] = t_tx >> 32;
    test_results[12] = t_tx & 0xFFFFFFFF;

    uint32_t other_eth_id = get_other_eth_id(eth01, parallel_cable_mode, parallel_loopback_mode, board_type);
    if ((!parallel_cable_mode) && (!parallel_loopback_mode)) {
      if (this_is_eth0) {
        other_eth_id = eth1_id;
      } else {
        other_eth_id = eth0_id;
      }
    }

    test_app_debug[0] = 0xcdef0003;
    
    volatile uint32_t* data_buf_ptr = dest_addr;
    test_app_debug[1] = (uint32_t)dest_addr;
   
    uint32_t exp_tile_seed = get_tile_rnd_seed(test_rnd_seed, other_eth_id, 0, tile_index);
    test_app_debug[0] = 0xcdef0004;
    test_results[13] = tile_index;
    test_results[14] = exp_tile_seed;
    uint32_t total_16B_words = data_buf_ptr[0];
    uint32_t found_tile_index = data_buf_ptr[1];
    uint32_t found_remote_index = data_buf_ptr[2];
    uint32_t found_tile_seed = data_buf_ptr[3];
    if ((found_tile_index != tile_index) ||
        (found_remote_index != other_eth_id) ||
        (found_tile_seed != exp_tile_seed)) { 
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+0] = 2;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+1] = (uint32_t)data_buf_ptr;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+2] = iter;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+3] = tile_index;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+8] = total_16B_words;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+9] = found_tile_index;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+10] = found_remote_index;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+11] = found_tile_seed;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+12] = total_16B_words;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+13] = tile_index;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+14] = other_eth_id;
      eth2eth_test_log_buf[eth2eth_test_log_buf_index+15] = exp_tile_seed;
      eth2eth_test_log_buf_index += 16;
      if (eth2eth_test_log_buf_index == (eth2eth_test_log_buf_size_dws)) {
        eth2eth_test_log_buf_index = 0;
      }
      fail = 1;
      errors_found++;
      status_val = TEST_END_TILE_HEADER_CHECK_FAIL;
      while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
      uint64_t read_addr = NOC_XY_ADDR(my_eth_x, my_eth_y, (uint32_t)&test_app_debug[2]); // Pretty much a dummy value, unused
      uint64_t wr_ptr_addr = NOC_XY_ADDR(eth0_x, eth0_y, (uint32_t)(&(test_results[4])));
      noc_atomic_read_and_increment (0, NCRISC_SMALL_TXN_CMD_BUF, wr_ptr_addr, 1, 31, read_addr, false, 0);
    }
    uint32_t num_data_errors = fast_prng_check(data_buf_ptr+4, total_16B_words-1, found_tile_seed, iter, tile_index);
    if (num_data_errors > 0 && !early_exit) {
      errors_found += num_data_errors;
      fail = 1;
      status_val = TEST_END_DATA_CHECK_FAIL;
      test_app_debug[0] = 0xcdef0005;
      while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
      uint64_t read_addr = NOC_XY_ADDR(my_eth_x, my_eth_y, (uint32_t)&test_app_debug[2]); // Pretty much a dummy value, unused
      uint64_t wr_ptr_addr = NOC_XY_ADDR(eth0_x, eth0_y, (uint32_t)(&(test_results[4])));
      noc_atomic_read_and_increment (0, NCRISC_SMALL_TXN_CMD_BUF, wr_ptr_addr, num_data_errors, 31, read_addr, false, 0);
    }
    
    bytes_checked += (uint64_t)(total_16B_words * 16);

    t_eth2eth_tx_end = eth_read_wall_clock();

    test_app_debug[0] = 0xcdef000a; 

    uint64_t t_check_done = eth_read_wall_clock();
    uint64_t t_check = t_check_done - t_tx_start;      
    test_results[15] = t_check >> 32;
    test_results[16] = t_check & 0xFFFFFFFF;

    // Unconditional risc context switch
    risc_context_switch();
  }

  test_app_debug[0] = 0xA0000003;

  uint64_t t_eth2eth_tx = t_eth2eth_tx_end - t_eth2eth_tx_start;
  
  test_results[17] = bytes_checked >> 32;
  test_results[18] = bytes_checked & 0xFFFFFFFF;
  test_results[19] = errors_found;

  test_results[20] = t_eth2eth_tx >> 32;
  test_results[21] = t_eth2eth_tx & 0xFFFFFFFF;
  test_results[22] = test_log_index;

  for (int l = 0; l < 4; l++) {
    uint32_t err_lo = eth_pcs_ind_reg_read(0x100d2+l*2);
    uint32_t err_hi = eth_pcs_ind_reg_read(0x100d3+l*2);
    uint32_t err = (err_hi << 16) + err_lo;
    test_results[43+l] = err;
  }

  // Get difference in corr and uncorr_cw counts and record it
  uint64_t final_corr_cw = ((uint64_t)results_buf[52] << 32) | (uint64_t)results_buf[53];
  uint64_t final_uncorr_cw = ((uint64_t)results_buf[54] << 32) | (uint64_t)results_buf[55];
  uint64_t corr_cw_diff = final_corr_cw - initial_corr_cw;
  uint64_t uncorr_cw_diff = final_uncorr_cw - initial_uncorr_cw;

  // If overflows, set to all f's to indicate that; but it should not overflow
  if (corr_cw_diff > 0xffffffff) corr_cw_diff = 0xffffffff;
  if (uncorr_cw_diff > 0xffffffff) uncorr_cw_diff = 0xffffffff;

  test_results[47] = corr_cw_diff;
  test_results[48] = uncorr_cw_diff;

  uint32_t pcs_baser_status = eth_pcs_ind_reg_read(0x30021);
  uint32_t high_ber = (pcs_baser_status >> 14) & 0x1;
  uint32_t ber_cnt = (pcs_baser_status >> 8) & 0x3f;
  test_results[49] = high_ber;
  test_results[50] = ber_cnt;
}

void run_packet_test() {
  noc_rd_issued[0] = NOC_READ_REG(NOC_STATUS(NIU_MST_RD_RESP_RECEIVED) + (0*NOC_INSTANCE_OFFSET));
  noc_rd_issued[1] = NOC_READ_REG(NOC_STATUS(NIU_MST_RD_RESP_RECEIVED) + (1*NOC_INSTANCE_OFFSET));

  uint32_t arg_index = 0;
  eth0_id = test_app_args[arg_index++]; 
  eth1_id = test_app_args[arg_index++];
  test_dry_run = test_app_args[arg_index++];

  test_num_iters = test_app_args[arg_index++];  

  parallel_loopback_mode = test_app_args[arg_index++];
  packet_mode = test_app_args[arg_index++];
  
  parallel_cable_mode = test_app_args[arg_index++];
  parallel_cable_enable_mask = test_app_args[arg_index++];
  
  board_type = test_app_args[arg_index++];

  test_remote_write = test_app_args[arg_index++];
  chip_version = test_app_args[arg_index++];

  test_rnd_seed = test_app_args[arg_index++];
  
  eth2eth_tx_data_buf_size = test_app_args[arg_index++];
  uint32_t disable_dest_ready_table = test_app_args[arg_index++];
  test_inject_error = test_app_args[arg_index++];
  test_inject_error_tx_eth = test_app_args[arg_index++];
  test_inject_error_iter = test_app_args[arg_index++];
  test_inject_error_bit = test_app_args[arg_index++];

  // uint32_t aiclk_ps = config_buf->aiclk_ps;
  // if (aiclk_ps == 0) aiclk_ps = 1;
  // uint8_t clk_scalar = BOOT_REFCLK_PERIOD_PS / (aiclk_ps);

  bool run_packet_test = false;  

  // first 5 are set to 0 by the test script
  for (uint32_t i = 5; i < test_results_size_bytes/4; i++) {
    test_results[i] = 0;
  }

  for (uint32_t i = 0; i < test_app_debug_size_bytes/4; i++) {
    test_app_debug[i] = 0;
  }

  my_eth_x = ETH_READ_REG(0xFFB2002C) & 0x3F;
  my_eth_y = (READ_REG(0xFFB2002C) >> 6) & 0x3F;
  test_results[5] = 0xFF000000 | (my_eth_y << 8) | my_eth_x;

  eth0_x = eth_id_get_x(eth0_id, 0);
  eth0_y = eth_id_get_y(eth0_id, 0);
  eth1_x = eth_id_get_x(eth1_id, 0);
  eth1_y = eth_id_get_y(eth1_id, 0);

  this_is_eth0 = ((my_eth_x == eth0_x) && (my_eth_y == eth0_y));
  this_is_eth1 = ((my_eth_x == eth1_x) && (my_eth_y == eth1_y));

  test_results[6] = (this_is_eth1 << 24) | (this_is_eth0 << 16) | (eth1_id << 8) | eth0_id;
  test_results[7] = (eth1_y << 24) | (eth1_x << 16) | (eth0_y << 8) | eth0_x;

  // Check for link enablement and set the parallel_cable_enable_mask dynamically
  // If parallel_cable_enable_mask is already set, ignore this step
  if (parallel_cable_mode && parallel_cable_enable_mask == 0) {
    uint32_t eth_my_index = eth_get_my_index();
    if (((config_buf->port_enable_force >> (ENABLE_CREDO_LOOPBACK_SHIFT + eth_my_index)) & 0x1) == 1) {
      // Keep in for backwards compatibility
      uint32_t rx_link_sta = eth_pcs_ind_reg_read(0x30001);
      if (rx_link_sta == 0x6) {
        parallel_cable_enable_mask |= 1 << eth_my_index;
      }
    } else if (my_node_info->train_status == LINK_TRAIN_SUCCESS) {
      parallel_cable_enable_mask |= 1 << eth_my_index;
    } else if (debug_buf[95] == 1) {
      parallel_cable_enable_mask |= 1 << eth_my_index;
      // Setup status checking but not retraining on these ports
      my_node_info->train_status = LINK_TRAIN_SUCCESS;
      config_buf->link_status_check_en = 0;
    }
    test_app_args[7] = parallel_cable_enable_mask;
  }

  if (disable_dest_ready_table) {
    NOC_STREAM_WRITE_REG(0, STREAM_DEBUG_STATUS_SEL_REG_INDEX, (0x1 << DISABLE_DEST_READY_TABLE));
  }
  
  for (uint32_t i = 0; i < 64; i++) {
    NOC_STREAM_WRITE_REG(i, STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX, 0);
  }


  if (node_is_eth(my_eth_x, my_eth_y)) {
    test_results[5] = 0xFF000001;

    if (parallel_cable_mode) {
      uint32_t eth_my_index = eth_get_my_index();
      if ((parallel_cable_enable_mask >> eth_my_index) & 0x1) run_packet_test = true;
    } else if (parallel_loopback_mode) {
      run_packet_test = true;
    } else if (this_is_eth0 || this_is_eth1) {
      run_packet_test = true;
    } else {}
      
    if (run_packet_test) {

      test_results[5] = 0xFF000002;

      // Note: packet mode should be enabled at this point, including cable loopback case.
      // tx/rx q 0 - enable packet mode
      // eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_HI, config_buf->bcast_mac_addr_high);
      // eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_LO, config_buf->bcast_mac_addr_low);
      // eth_txq_reg_write(0, ETH_TXQ_MAX_PKT_SIZE_BYTES, config_buf->max_packet_size_bytes);
      // eth_txq_reg_write(0, ETH_TXQ_REMOTE_SEQ_TIMEOUT, config_buf->remote_seq_timeout);
      // eth_txq_reg_write(0, ETH_TXQ_BURST_LEN, config_buf->burst_len);
      // eth_txq_reg_write(0, ETH_TXQ_MIN_PACKET_SIZE_WORDS, config_buf->min_packet_size_words);
      // eth_txq_reg_write(0, ETH_TXQ_LOCAL_SEQ_UPDATE_TIMEOUT, config_buf->local_seq_update_timeout);

      // eth_txq_reg_write(0, ETH_TXQ_CTRL, (0x1 << 3) | (0x1 << 0)); // enable packet resend, disable drop notifications (i.e. timeout-only resend mode) to avoid #2943 issues

      if (!packet_mode) {
        eth_rxq_init(0, ((uint32_t)(eth2eth_rx_data_buf_0)) >> 4, (eth2eth_tx_data_buf_size) >> 4, false, false);
        eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_HI, config_buf->bcast_mac_addr_high);
        eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_LO, config_buf->bcast_mac_addr_low);
        eth_txq_reg_write(0, ETH_TXQ_CTRL, 0x0);
        eth_txq_reg_write(0, ETH_TXQ_TRANSFER_SIZE_BYTES, eth2eth_tx_data_buf_size);
        eth_txq_reg_write(0, ETH_TXQ_MAX_PKT_SIZE_BYTES, config_buf->max_packet_size_bytes);

        eth_wait_cycles(1000000);
      }

      test_results[5] = 0xFF000003;

      if (test_remote_write) {
        test_results[5] = 0xFF000004;
        test_remote_reg_writes_simple();
        finish_test(TEST_END_ETH_ACTIVE, 0);
      } else {
        test_results[5] = 0xFF000005;
        test_traffic_packet_eth(eth_get_my_index());
        finish_test(TEST_END_ETH_ACTIVE, 0);
      }
    } else {
      test_results[5] = 0xFF000006;
      finish_test(TEST_END_ETH_NOT_ACTIVE, 0);
    }
  } else {
    test_results[5] = 0xFF000007;
    finish_test(TEST_END_REACHED_MAIN_END, 1);
  }
}

void ApplicationHandler(void)
{
  init_rtos_table();

  ncrisc_noc_counters_init();
  debug_buf[97] = (ETH_APP_VERSION_MAJOR << 16) | (ETH_APP_VERSION_MINOR << 8) | ETH_APP_VERSION_TEST;;
  if (config_buf->run_test_app) {
    run_packet_test();
  }
}
