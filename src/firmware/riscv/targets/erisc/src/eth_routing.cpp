// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>

#include "eth_routing.h"
#include "eth_init.h"
#include "eth_ss.h"
#include "noc.h"
#include "noc_nonblocking_api.h"


static eth_queues_t *eth_queues;
static uint32_t mac_rcv_fail;
static uint64_t next_timestamp;

volatile uint32_t noc_scratch_buf[8] __attribute__((aligned(32)));

uint32_t remote_chip_id_src[4]  __attribute__((aligned(16)));
volatile uint32_t remote_chip_id_dest[4] __attribute__((aligned(16)));
uint8_t router_chip_scratch_src[32]  __attribute__((aligned(16)));
volatile uint8_t router_chip_scratch_dest[32] __attribute__((aligned(16)));

volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
volatile eth_conn_info_t* const eth_conn_info = (volatile eth_conn_info_t*)(ETH_CONN_INFO_ADDR);
volatile system_connection_t* sys_connections = (volatile system_connection_t*)(ETH_CONN_INFO_ADDR);
node_info_t* const my_node_info = (node_info_t*)(NODE_INFO_ADDR);

volatile uint32_t* const test_results = (volatile uint32_t*)(RESULTS_BUF_ADDR);
volatile uint32_t* const debug_buf = (volatile uint32_t*)(DEBUG_BUF_ADDR);
volatile uint32_t* const tmp_2k_buf = (volatile uint32_t*)(TEMP_2K_BUF_ADDR);

// FIXME temp solution to clear structs
volatile uint32_t* const tmp_buf = (volatile uint32_t*)(NODE_INFO_ADDR);

#define ROUTING_VC 0
#define ROUTING_NOC 0


void init_timestamps() {
  uint32_t aiclk_ps = config_buf->aiclk_ps;
  if (aiclk_ps == 0) aiclk_ps = 1;
  uint8_t clk_scalar = BOOT_REFCLK_PERIOD_PS / (aiclk_ps);

  uint32_t wait_cycles = 27000 * clk_scalar * 100; //~100ms
  uint64_t curr_timestamp = eth_read_wall_clock();
  next_timestamp = curr_timestamp + wait_cycles;
}


void eth_link_init() {
    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 1);

    eth_rxq_init(0, 0, 0, false, true);

    for (int i = 0; i < 4; i++) {
      remote_chip_id_dest[i] = 0;
    }

    for (int i = 0; i < 32; i++) {
      router_chip_scratch_src[i] = 0;
      router_chip_scratch_dest[i] = 0;
    }

    remote_chip_id_src[0] = my_node_info->my_eth_id;
    remote_chip_id_src[1] = (my_node_info->my_chip_y << 8) | my_node_info->my_chip_x;
    remote_chip_id_src[2] = (my_node_info->my_rack_y << 8) | my_node_info->my_rack_x;
    remote_chip_id_src[3] = 1;

    // tx/rx q 1 - enable raw data mode, used only for post-training crc checks
    uint64_t unicast_mac_addr = 0x0000000000aa;
    eth_txq_reg_write(1, ETH_TXQ_DEST_MAC_ADDR_HI, unicast_mac_addr >> 32);
    eth_txq_reg_write(1, ETH_TXQ_DEST_MAC_ADDR_LO, unicast_mac_addr & 0xFFFFFFFF);
    eth_txq_reg_write(1, ETH_TXQ_TRANSFER_START_ADDR, (uint32_t)tmp_2k_buf);
    eth_txq_reg_write(1, ETH_TXQ_TRANSFER_SIZE_BYTES, 1024);
    eth_txq_reg_write(1, ETH_TXQ_CTRL, 0x0);
    eth_rxq_init(1, (((uint32_t)tmp_2k_buf) + 1024) >> 4, 1024 >> 4, true,  false);

    // initialize the tmp buf
    uint32_t prng = prng_next(0xabcd);
    for (int w = 0; w < (1024/4); w++) {
      prng = prng_next(prng);
      tmp_2k_buf[w] = prng;
    }


    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 2);
    // eth link init and training
    link_train_status_e train_status;
    train_status = eth_link_start_handler(config_buf, LINK_POWERUP, false);
    my_node_info->train_status = train_status;

    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 3);

    if (train_status != LINK_TRAIN_SUCCESS) return;

    remote_chip_id_dest[0] = test_results[76];
    remote_chip_id_dest[1] = test_results[74];
    remote_chip_id_dest[2] = test_results[75];
    remote_chip_id_dest[3] = 1;

    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 4);
    // tx/rx q 0 - enable packet mode
    // eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_HI, config_buf->bcast_mac_addr_high);
    // eth_txq_reg_write(0, ETH_TXQ_DEST_MAC_ADDR_LO, config_buf->bcast_mac_addr_low);
    // eth_txq_reg_write(0, ETH_TXQ_MAX_PKT_SIZE_BYTES, config_buf->max_packet_size_bytes);
    // eth_txq_reg_write(0, ETH_TXQ_REMOTE_SEQ_TIMEOUT, config_buf->remote_seq_timeout);
    // eth_txq_reg_write(0, ETH_TXQ_BURST_LEN, config_buf->burst_len);
    // eth_txq_reg_write(0, ETH_TXQ_MIN_PACKET_SIZE_WORDS, config_buf->min_packet_size_words);
    // eth_txq_reg_write(0, ETH_TXQ_LOCAL_SEQ_UPDATE_TIMEOUT, config_buf->local_seq_update_timeout);

    // eth_txq_reg_write(0, ETH_TXQ_CTRL, (0x1 << 3) | (0x1 << 0)); // enable packet resend, disable drop notifications (i.e. timeout-only resend mode) to avoid #2943 issues

    // eth_wait_cycles(1000000);

    // eth_send_packet(0, ((uint32_t)remote_chip_id_src)>>4, ((uint32_t)remote_chip_id_dest)>>4, 1);

    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 5);
    // while (remote_chip_id_dest[3] == 0);
    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 6);
}


void __attribute__((section(".misc_init"))) init_routing_connections() {

  int min_eth_id_u = -1;
  int min_eth_id_r = -1;
  int min_eth_id_d = -1;
  int min_eth_id_l = -1;
  int min_eth_id_rack_x = -1;
  int min_eth_id_rack_y = -1;

  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    if ((min_eth_id_u == -1) && (eth_conn_info[e] == ETH_CONN_U)) {
      min_eth_id_u = e;
    }
    if ((min_eth_id_r == -1) && (eth_conn_info[e] == ETH_CONN_R)) {
      min_eth_id_r = e;
    }
    if ((min_eth_id_d == -1) && (eth_conn_info[e] == ETH_CONN_D)) {
      min_eth_id_d = e;
    }
    if ((min_eth_id_l == -1) && (eth_conn_info[e] == ETH_CONN_L)) {
      min_eth_id_l = e;
    }
    if ((min_eth_id_rack_x == -1) && (eth_conn_info[e] == ETH_RACK_R || eth_conn_info[e] == ETH_RACK_L)) {
      min_eth_id_rack_x = e;
    }
    if ((min_eth_id_rack_y == -1) && (eth_conn_info[e] == ETH_RACK_U || eth_conn_info[e] == ETH_RACK_D)) {
      min_eth_id_rack_y = e;
    }
  }

  // TODO: add error conditions for when the above links can't be found

  uint32_t remote_eth_chip_x = remote_chip_id_dest[1] & CHIP_COORD_MASK;
  uint32_t remote_eth_chip_y = (remote_chip_id_dest[1] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
  uint32_t remote_eth_id = remote_chip_id_dest[0];
  my_node_info->remote_eth_sys_addr = get_sys_addr(remote_eth_chip_x, remote_eth_chip_y, eth_id_get_x(remote_eth_id), eth_id_get_y(remote_eth_id), 0);
  my_node_info->remote_eth_rack = remote_chip_id_dest[2];
  uint32_t my_chip_x = my_node_info->my_chip_x;
  uint32_t my_chip_y = my_node_info->my_chip_y;
  uint32_t my_eth_id = my_node_info->my_eth_id;

  // TODO: review this
  // all x directions are routed from the first row, e.g. y=0
  my_node_info->routing_node = (my_eth_id == (uint32_t)min_eth_id_l) || (my_eth_id == (uint32_t)min_eth_id_r) || (my_eth_id == (uint32_t)min_eth_id_u) || (my_eth_id == (uint32_t)min_eth_id_d) ||
                               (my_eth_id == (uint32_t)min_eth_id_rack_x) || (my_eth_id == (uint32_t)min_eth_id_rack_y);

  my_node_info->dest_x_route_l_sys_addr = min_eth_id_l == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_l),
                                                    eth_id_get_y(min_eth_id_l));
  my_node_info->dest_x_route_r_sys_addr = min_eth_id_r == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_r),
                                                    eth_id_get_y(min_eth_id_r));
  my_node_info->dest_y_route_u_sys_addr = min_eth_id_u == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_u),
                                                    eth_id_get_y(min_eth_id_u));
  my_node_info->dest_y_route_d_sys_addr = min_eth_id_d == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_d),
                                                    eth_id_get_y(min_eth_id_d));
  my_node_info->rack_route_x_sys_addr = min_eth_id_rack_x == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_rack_x),
                                                    eth_id_get_y(min_eth_id_rack_x));
  my_node_info->rack_route_y_sys_addr = min_eth_id_rack_y == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_rack_y),
                                                    eth_id_get_y(min_eth_id_rack_y));
  my_node_info->src_dest_index = noc_xy_get_erisc_index(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0]);

  my_node_info->src_sys_addr[eth_q_id_t::ROOT] = my_node_info->root_node ? -1 : get_sys_addr(my_chip_x, my_chip_y, ROOT_NODE_NOC_X, ROOT_NODE_NOC_Y,0xaaa0);
  my_node_info->src_rack[eth_q_id_t::ROOT] = remote_chip_id_src[2];
  my_node_info->src_sys_addr[eth_q_id_t::TOP] = -1;
  my_node_info->src_sys_addr[eth_q_id_t::RIGHT] = -1;
  my_node_info->src_sys_addr[eth_q_id_t::BOTTOM] = -1;
  my_node_info->src_sys_addr[eth_q_id_t::LEFT] = -1;

  if (my_node_info->routing_node) {
    my_node_info->src_sys_addr[eth_q_id_t::TOP] = (uint64_t)my_node_info->dest_y_route_u_sys_addr << NOC_ADDR_LOCAL_BITS;
    my_node_info->src_rack[eth_q_id_t::TOP] = remote_chip_id_src[2];
    my_node_info->src_sys_addr[eth_q_id_t::RIGHT] = (uint64_t)my_node_info->dest_x_route_r_sys_addr << NOC_ADDR_LOCAL_BITS;
    my_node_info->src_rack[eth_q_id_t::RIGHT] = remote_chip_id_src[2];
    my_node_info->src_sys_addr[eth_q_id_t::BOTTOM] = (uint64_t)my_node_info->dest_y_route_d_sys_addr << NOC_ADDR_LOCAL_BITS;
    my_node_info->src_rack[eth_q_id_t::BOTTOM] = remote_chip_id_src[2];
    my_node_info->src_sys_addr[eth_q_id_t::LEFT] = (uint64_t)my_node_info->dest_x_route_l_sys_addr << NOC_ADDR_LOCAL_BITS;
    my_node_info->src_rack[eth_q_id_t::LEFT] = remote_chip_id_src[2];

    if (min_eth_id_rack_y != -1) {
      if (my_chip_x == 0) {
        my_node_info->src_sys_addr[eth_q_id_t::LEFT] = (uint64_t)my_node_info->rack_route_y_sys_addr << NOC_ADDR_LOCAL_BITS;
      } else {
        my_node_info->src_sys_addr[eth_q_id_t::RIGHT] = (uint64_t)my_node_info->rack_route_y_sys_addr << NOC_ADDR_LOCAL_BITS;
      }
    }

    if (min_eth_id_rack_x != -1) {
      if (my_chip_y == 0) {
        my_node_info->src_sys_addr[eth_q_id_t::BOTTOM] = (uint64_t)my_node_info->rack_route_x_sys_addr << NOC_ADDR_LOCAL_BITS;
      } else {
        my_node_info->src_sys_addr[eth_q_id_t::TOP] = (uint64_t)my_node_info->rack_route_x_sys_addr << NOC_ADDR_LOCAL_BITS;
      }
    }

    eth_q_id_t q_id = eth_q_id_t::ROOT;
    if (my_eth_id == (uint32_t)min_eth_id_u) {
      q_id = eth_q_id_t::TOP;
    } else if (my_eth_id == (uint32_t)min_eth_id_r) {
      q_id = eth_q_id_t::RIGHT;
    } else if (my_eth_id == (uint32_t)min_eth_id_d) {
      q_id = eth_q_id_t::BOTTOM;
    } else if (my_eth_id == (uint32_t)min_eth_id_l) {
      q_id = eth_q_id_t::LEFT;
    } else if (my_eth_id == (uint32_t)min_eth_id_rack_y) {
      q_id = my_chip_x == 0 ? eth_q_id_t::LEFT : eth_q_id_t::RIGHT;
    } else if (my_eth_id == (uint32_t)min_eth_id_rack_x) {
      q_id = my_chip_y == 0 ? eth_q_id_t::BOTTOM : eth_q_id_t::TOP;
    }
    if (q_id != eth_q_id_t::ROOT) {
      my_node_info->src_sys_addr[q_id] = my_node_info->remote_eth_sys_addr;
      my_node_info->src_rack[q_id] = remote_chip_id_dest[2];
      my_node_info->eth_req_q_id = q_id;
    }
  }

  for (int i = 0; i < eth_q_id_t::NUM_Q; i++) {
    my_node_info->src_rdptr_val[i] = 0;
  }
  my_node_info->delimiter = 0xabcd1234;

}

void copy_to_all_eth (uint32_t addr)
{
  // now send info to every tensix/eth in the chip
  for (uint32_t x = 0; x < NOC_SIZE_X; x++) {
    for (uint32_t y = 0; y < NOC_SIZE_Y; y++) {
      // if (node_is_eth(x, y) || node_is_tensix(x, y)) {
      if (node_is_eth(x, y)) {
        /*
        noc_copy(NOC_XY_ADDR(my_node_info->my_noc_x, my_node_info->my_noc_y, (uint32_t)(eth_conn_info + my_node_info->my_eth_id)),
                 NOC_XY_ADDR(x, y, (uint32_t)(eth_conn_info + my_node_info->my_eth_id)),
                 4, false, true, true, 0, 0, 0);
        */
        while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
        ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        addr,
                        NOC_XY_ADDR(x, y, addr),
                        4,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
      }
    }
  }
}

bool is_chip_edge_router (uint32_t chip_x, uint32_t chip_y)
{
  return ((chip_x == 0) || (chip_x == MAX_BOARD_CONNECTIONS_X - 1) || (chip_y == 0) || (chip_y == MAX_BOARD_CONNECTIONS_Y - 1));
}

uint32_t get_chip_x_from_router_index(uint32_t i)
{
  uint32_t x;
  if (i <= 7) {
    x = 0;
  } else if (i <= 11) {
    x = i - 8;
  } else if (i <= 19) {
    x = 3;
  } else {
    x = 23 - i;
  }
  return x;
}

uint32_t get_chip_y_from_router_index(uint32_t i)
{
  uint32_t y;
  if (i <= 7) {
    y = i;
  } else if (i <= 11) {
    y = 7;
  } else if (i <= 19) {
    y = 19 - i;
  } else {
    y = 0;
  }
  return y;
}

int32_t get_y_router_chip_index(uint32_t chip_x, uint32_t chip_y)
{
  int32_t chip_index = -1;
  if (chip_x == 0) {
    // 0 - 7
    chip_index = chip_y;
  } else if (chip_x == 3) {
    // 12 - 19
    chip_index = 2 * MAX_BOARD_CONNECTIONS_Y + MAX_BOARD_CONNECTIONS_X - 1 - chip_y;
  }
  return chip_index;
}

int32_t get_x_router_chip_index(uint32_t chip_x, uint32_t chip_y)
{
  int32_t chip_index = -1;
  if (chip_y == 7) {
    // 8 - 11
    chip_index = chip_x + MAX_BOARD_CONNECTIONS_Y;
  } else if (chip_y == 0) {
    // 20 - 23
    chip_index = 2 * MAX_BOARD_CONNECTIONS_Y + 2 * MAX_BOARD_CONNECTIONS_X - 1 - chip_x;
  }
  return chip_index;
}

void __attribute__((section(".misc_init"))) wait_for_phase(routing_discovery_phase_t phase)
{
  while (sys_connections->routing_discovery_phase != phase);
}
void __attribute__((section(".misc_init"))) wait_for_edge_connection_info(int32_t router_chip_index, uint32_t eth_id_prev, uint32_t eth_id_next, routing_discovery_phase_t phase)
{
    if (my_node_info->my_eth_id == eth_id_next) {
      if (router_chip_index != -1) {
        while (sys_connections->rack_router_chips[router_chip_index] == ETH_UNKNOWN);
      }

      //Wait for previous chip in edge ring to send its rack connection info.
      while (sys_connections->routing_discovery_phase != phase) {eth_link_status_check();}

      uint32_t words_to_copy = router_chip_index == -1 ? sys_connections->router_chip_count : router_chip_index;

      for (uint32_t i = 0; i <= words_to_copy; i++) {
        router_chip_scratch_src[i] = sys_connections->rack_router_chips[i];
      }
      router_chip_scratch_src[MAX_EDGE_ROUTERS] = (uint8_t)words_to_copy;
      router_chip_scratch_src[MAX_EDGE_ROUTERS+1] = (uint8_t)sys_connections->routing_discovery_phase;

      eth_send_packet(0, ((uint32_t)router_chip_scratch_src)>>4, ((uint32_t)router_chip_scratch_dest)>>4, 2);
    }

    if (my_node_info->my_eth_id == eth_id_prev) {

        //Wait for previous chip in edge ring to send its rack connection info.
        while (router_chip_scratch_dest[MAX_EDGE_ROUTERS+1] != phase) {eth_link_status_check();}

        uint32_t words_to_copy = router_chip_scratch_dest[MAX_EDGE_ROUTERS];

        for (uint32_t i = 0; i <= words_to_copy; i++) {
          sys_connections->rack_router_chips[i] = router_chip_scratch_dest[i];
          router_chip_scratch_dest[i] = 0;
          copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[i]);
        }

        sys_connections->router_chip_count = router_chip_index == -1 ? words_to_copy : words_to_copy + 1;

        sys_connections->routing_discovery_phase = (routing_discovery_phase_t)router_chip_scratch_dest[MAX_EDGE_ROUTERS+1];
        router_chip_scratch_dest[MAX_EDGE_ROUTERS] = 0;
        router_chip_scratch_dest[MAX_EDGE_ROUTERS+1] = 0;

        copy_to_all_eth((uint32_t)&sys_connections->router_chip_count);
        copy_to_all_eth((uint32_t)&sys_connections->routing_discovery_phase);

    }
}

void __attribute__((section(".misc_init"))) relay_full_edge_connection_info(uint32_t eth_id_prev, uint32_t eth_id_next, routing_discovery_phase_t phase)
{
    if (my_node_info->my_eth_id == eth_id_next) {
      //Wait for previous chip in edge ring to send its rack connection info.
      while (sys_connections->routing_discovery_phase != phase) {eth_link_status_check();}

      for (int i = 0; i < MAX_EDGE_ROUTERS; i++) {
        router_chip_scratch_src[i] = sys_connections->rack_router_chips[i];
      }
      router_chip_scratch_src[MAX_EDGE_ROUTERS] = sys_connections->router_chip_count;
      router_chip_scratch_src[MAX_EDGE_ROUTERS+1] = sys_connections->routing_discovery_phase;
      eth_send_packet(0, ((uint32_t)router_chip_scratch_src)>>4, ((uint32_t)router_chip_scratch_dest)>>4, 2);
    }

    if (my_node_info->my_eth_id == eth_id_prev) {
        //Wait for previous chip in edge ring to send its rack connection info.
        while (router_chip_scratch_dest[MAX_EDGE_ROUTERS+1] != phase) {eth_link_status_check();}

        for (int i = 0; i < MAX_EDGE_ROUTERS; i++) {
          sys_connections->rack_router_chips[i] = router_chip_scratch_dest[i];
          router_chip_scratch_dest[i] = 0;
          copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[i]);
        }
        sys_connections->router_chip_count = router_chip_scratch_dest[MAX_EDGE_ROUTERS];
        sys_connections->routing_discovery_phase = (routing_discovery_phase_t)router_chip_scratch_dest[MAX_EDGE_ROUTERS+1];
        router_chip_scratch_dest[MAX_EDGE_ROUTERS] = 0;
        router_chip_scratch_dest[MAX_EDGE_ROUTERS+1] = 0;
        copy_to_all_eth((uint32_t)&sys_connections->router_chip_count);
        copy_to_all_eth((uint32_t)&sys_connections->routing_discovery_phase);
    }
}

void __attribute__((section(".misc_init"))) signal_phase(routing_discovery_phase_t phase)
{
        sys_connections->routing_discovery_phase = phase;
        copy_to_all_eth((uint32_t)&sys_connections->routing_discovery_phase);
}

void __attribute__((noinline)) __attribute__((section(".misc_init"))) populate_missing_routers_low(uint32_t my_chip_x, uint32_t my_chip_y, eth_conn_info_t direction)
{
  bool copy_chip_count = false;
  if (direction == eth_conn_info_t::ETH_CONN_L) {
    for (int y = 0; y < MAX_BOARD_CONNECTIONS_Y; y++) {
      if (y < (int)my_chip_y || my_chip_x != 0) {
        int32_t router_chip_index = get_y_router_chip_index(0, y);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_chip_count = true;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_R) {
    for (int y = MAX_BOARD_CONNECTIONS_Y - 1; y >=0; y--) {
      if (y > (int)my_chip_y || my_chip_x != MAX_BOARD_CONNECTIONS_X-1) {
        int32_t router_chip_index = get_y_router_chip_index(MAX_BOARD_CONNECTIONS_X-1, y);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_chip_count = true;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_U) {
    for (int x = 0; x < MAX_BOARD_CONNECTIONS_X; x++) {
      if (x < (int)my_chip_x || my_chip_y != MAX_BOARD_CONNECTIONS_Y-1) {
        int32_t router_chip_index = get_x_router_chip_index(x, MAX_BOARD_CONNECTIONS_Y-1);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_chip_count = true;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_D) {
    for (int x = MAX_BOARD_CONNECTIONS_X - 1; x >= 0; x--) {
      if (x > (int)my_chip_x || my_chip_y != 0) {
        int32_t router_chip_index = get_x_router_chip_index(x, 0);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_chip_count = true;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  }
  if (copy_chip_count) {
    copy_to_all_eth((uint32_t)&sys_connections->router_chip_count);
  }
}

void __attribute__((noinline)) __attribute__((section(".misc_init"))) populate_missing_routers_high(uint32_t my_chip_x, uint32_t my_chip_y, eth_conn_info_t direction)
{

  if (direction == eth_conn_info_t::ETH_CONN_L) {
    if (my_chip_x == 0) {
      for (int y = my_chip_y + 1; y < MAX_BOARD_CONNECTIONS_Y; y++) {
        int32_t router_chip_index = get_y_router_chip_index(my_chip_x, y);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_R) {
    if (my_chip_x == MAX_BOARD_CONNECTIONS_X - 1) {
      for (int y = my_chip_y - 1; y >= 0; y--) {
        int32_t router_chip_index = get_y_router_chip_index(my_chip_x, y);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_U) {
    if (my_chip_y == MAX_BOARD_CONNECTIONS_Y - 1) {
      for (int x = my_chip_x + 1; x < MAX_BOARD_CONNECTIONS_X; x++) {
        int32_t router_chip_index = get_x_router_chip_index(x, my_chip_y);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_D) {
    if (my_chip_y == 0) {
      for (int x = my_chip_x - 1; x >= 0; x--) {
        int32_t router_chip_index = get_x_router_chip_index(x, my_chip_y);
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        sys_connections->router_chip_count = router_chip_index;
        copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
      }
    }
  }
  //sys_connections->router_chip_count++;
  copy_to_all_eth((uint32_t)&sys_connections->router_chip_count);
}

void my_node_info_init() {

  // zero out all queue/pointer structures:
  volatile uint32_t* ptr = ((volatile uint32_t*)ETH_ROUTING_STRUCT_ADDR);
  for (uint32_t i = 0; i < ETH_QUEUES_SIZE_BYTES/4; i++) {
    ptr[i] = 0;
  }

  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    eth_conn_info[e] = ETH_UNKNOWN;
  }

  //set all router chips for rack connections to invalid
  for (uint32_t r = 0; r < MAX_EDGE_ROUTERS ; r++) {
    sys_connections->rack_router_chips[r] = 0;
  }
  sys_connections->router_chip_count = 0;
  sys_connections->routing_discovery_phase = PHASE_0;

  // first 5 are set to 0 by the test script
  for (uint32_t i = 5; i < RESULTS_BUF_SIZE/4; i++) {
    test_results[i] = 0;
  }

  // clear node info and eth conn buf
  for (uint32_t i = 0; i < (NODE_INFO_SIZE + ETH_CONN_INFO_SIZE); i++) {
    tmp_buf[i] = 0;
  }

  // clear config buffer space
  for (uint32_t i = 0; i < (DEBUG_BUF_SIZE)/4; i++) {
    debug_buf[i] = 0;
  }

  mac_rcv_fail = 0;

  init_timestamps();

  ////

  // TODO: add actual init for the lines marked with TODO
  // config_buf->local_chip_coord = 0x101;

  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    uint32_t noc_id_reg = NOC_CMD_BUF_READ_REG(n, 0, NOC_NODE_ID);
    my_node_info->my_noc_x[n] = noc_id_reg & NOC_NODE_ID_MASK;
    my_node_info->my_noc_y[n] = (noc_id_reg >> NOC_ADDR_NODE_ID_BITS) & NOC_NODE_ID_MASK;
  }

  uint32_t my_chip_x = (config_buf->local_chip_coord >> CHIP_COORD_X_SHIFT) & CHIP_COORD_MASK;
  uint32_t my_chip_y = (config_buf->local_chip_coord >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
  uint32_t my_rack_x = (config_buf->shelf_rack_coord >> CHIP_COORD_X_SHIFT) & CHIP_COORD_MASK;
  uint32_t my_rack_y = (config_buf->shelf_rack_coord >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
  my_node_info->my_chip_x = my_chip_x;
  my_node_info->my_chip_y = my_chip_y;
  my_node_info->my_rack_x = my_rack_x;
  my_node_info->my_rack_y = my_rack_y;



  my_node_info->eth_node = 1;//UC Remove this.
  uint32_t my_eth_id;
  if (my_node_info->my_noc_x[0] < 5) {
    my_eth_id = (my_node_info->my_noc_x[0] * 2 - 1) + (my_node_info->my_noc_y[0] == 0 ? 0 : 8);
  } else {
    my_eth_id = (9 - my_node_info->my_noc_x[0]) * 2 + (my_node_info->my_noc_y[0] == 0 ? 0 : 8);
  }
  my_node_info->my_eth_id = my_eth_id;

  my_node_info->root_node =
    //(my_node_info->my_chip_x == my_node_info->root_chip_x) &&
    //(my_node_info->my_chip_y == my_node_info->root_chip_y) &&
    (my_node_info->my_noc_x[0] == ROOT_NODE_NOC_X) &&
    (my_node_info->my_noc_y[0] == ROOT_NODE_NOC_Y);

  // start the ethernet link & exchange eth/chip id info with the other side
  eth_link_init();

  eth_conn_info_t conn_info = ETH_UNCONNECTED;;

  if (remote_chip_id_dest[3] == 0) {
    conn_info = ETH_UNCONNECTED;
  } else {
    uint32_t remote_chip_x = (remote_chip_id_dest[1] >> CHIP_COORD_X_SHIFT) & CHIP_COORD_MASK;
    uint32_t remote_chip_y = (remote_chip_id_dest[1] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
    uint32_t remote_rack_x = (remote_chip_id_dest[2] >> CHIP_COORD_X_SHIFT) & CHIP_COORD_MASK;
    uint32_t remote_rack_y = (remote_chip_id_dest[2] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
    if (remote_rack_x > my_rack_x) {
      conn_info = ETH_RACK_R;
    } else if (remote_rack_x < my_rack_x) {
      conn_info = ETH_RACK_L;
    } else if (remote_rack_y > my_rack_y) {
      conn_info = ETH_RACK_U;
    } else if (remote_rack_y < my_rack_y) {
      conn_info = ETH_RACK_D;
    } else if (remote_chip_x > my_chip_x) {
      conn_info = ETH_CONN_R;
    } else if (remote_chip_x < my_chip_x) {
      conn_info = ETH_CONN_L;
    } else if (remote_chip_y > my_chip_y) {
      conn_info = ETH_CONN_U;
    } else {
      conn_info = ETH_CONN_D;
    }
  }

  eth_conn_info[my_eth_id] = conn_info;;

  copy_to_all_eth((uint32_t)(eth_conn_info + my_eth_id));

  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    while (eth_conn_info[e] == ETH_UNKNOWN);
  }

  //Determine connectivity in Y/Vertical direction.
  //This is shelf to shelf connection on the same Rack.
  if ((my_chip_x == 0) || (my_chip_x == (MAX_BOARD_CONNECTIONS_X - 1))) {
    if (my_eth_id == 0) {
      int min_eth_id_u = -1;
      int min_eth_id_d = -1;
      uint32_t router_chip_index = get_y_router_chip_index(my_chip_x, my_chip_y);
      for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
        if (min_eth_id_u == -1 && eth_conn_info[e] == ETH_RACK_U) {
          min_eth_id_u = e;
        }
        if (min_eth_id_d == -1 && eth_conn_info[e] == ETH_RACK_D) {
          min_eth_id_d = e;
        }
      }

      if (min_eth_id_u != -1) {
        sys_connections->rack_router_chips[router_chip_index] = ((int16_t)min_eth_id_u << 4) | ETH_RACK_U;
      } else if (min_eth_id_d != -1) {
        sys_connections->rack_router_chips[router_chip_index] = ((int16_t)min_eth_id_d << 4) | ETH_RACK_D;
      } else {
        sys_connections->rack_router_chips[router_chip_index] = (0xf << 4) | ETH_UNCONNECTED;
      }
      copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
    }
  }

  //Determine connectivity in X/Horizontal direction.
  //This is shelf to shelf connection on different Racks.
  if ((my_chip_y == 0) || (my_chip_y == (MAX_BOARD_CONNECTIONS_Y - 1))) {
    if (my_eth_id == 0) {
      int min_eth_id_l = -1;
      int min_eth_id_r = -1;
      uint32_t router_chip_index = get_x_router_chip_index(my_chip_x, my_chip_y);
      for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
        if (min_eth_id_l == -1 && eth_conn_info[e] == ETH_RACK_L) {
          min_eth_id_l = e;
        }
        if (min_eth_id_r == -1 && eth_conn_info[e] == ETH_RACK_R) {
          min_eth_id_r = e;
        }
      }

      if (min_eth_id_l != -1) {
        sys_connections->rack_router_chips[router_chip_index] = ((uint16_t)min_eth_id_l << 4) | ETH_RACK_L;
      } else if (min_eth_id_r != -1) {
        sys_connections->rack_router_chips[router_chip_index] = ((uint16_t)min_eth_id_r << 4) | ETH_RACK_R;
      } else {
        sys_connections->rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
      }
      copy_to_all_eth((uint32_t)&sys_connections->rack_router_chips[router_chip_index]);
    }
  }

  bool left_edge = false;
  bool right_edge = false;
  bool top_edge = false;
  bool bot_edge = false;
  bool edge_chip = false;

  //propagate Y router chips.
  //if I am the minimum eth connected in U or D direction,
  int min_eth_id_u = -1;
  int min_eth_id_r = -1;
  int min_eth_id_d = -1;
  int min_eth_id_l = -1;

  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    if ((min_eth_id_u == -1) && (eth_conn_info[e] == ETH_CONN_U)) {
      min_eth_id_u = e;
    }
    if ((min_eth_id_r == -1) && (eth_conn_info[e] == ETH_CONN_R)) {
      min_eth_id_r = e;
    }
    if ((min_eth_id_d == -1) && (eth_conn_info[e] == ETH_CONN_D)) {
      min_eth_id_d = e;
    }
    if ((min_eth_id_l == -1) && (eth_conn_info[e] == ETH_CONN_L)) {
      min_eth_id_l = e;
    }
  }
  left_edge = min_eth_id_l == -1;
  right_edge = min_eth_id_r == -1;
  top_edge = min_eth_id_u == -1;
  bot_edge = min_eth_id_d == -1;
  edge_chip = left_edge || right_edge || top_edge || bot_edge;
  
  if (edge_chip)
  {
    int32_t router_chip_index;
    if (left_edge) {
      if (bot_edge && my_eth_id == 0) {
        populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_L);
        signal_phase(routing_discovery_phase_t::PHASE_1);
      }
      router_chip_index = -1;
      if (my_chip_x == 0) {
        router_chip_index = get_y_router_chip_index(my_chip_x, my_chip_y);
      }
      wait_for_edge_connection_info(router_chip_index, min_eth_id_d, min_eth_id_u, routing_discovery_phase_t::PHASE_1);
      uint32_t worker_eth_id = min_eth_id_d == -1 ? 0 : min_eth_id_d;
      if (top_edge && my_eth_id == worker_eth_id) {
        wait_for_phase(routing_discovery_phase_t::PHASE_1);
        populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_L);

        populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_U);
        signal_phase(routing_discovery_phase_t::PHASE_2);
      }
    }

    if (top_edge) {
      router_chip_index = -1;
      if (my_chip_y == MAX_BOARD_CONNECTIONS_Y-1) {
        router_chip_index = get_x_router_chip_index(my_chip_x, my_chip_y);
      }
      wait_for_edge_connection_info(router_chip_index, min_eth_id_l, min_eth_id_r, routing_discovery_phase_t::PHASE_2);
      uint32_t worker_eth_id = min_eth_id_l == -1 ? 0 : min_eth_id_l;
      if (right_edge && my_eth_id == worker_eth_id) {
        wait_for_phase(routing_discovery_phase_t::PHASE_2);
        populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_U);

        populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_R);
        signal_phase(routing_discovery_phase_t::PHASE_3);
      }
    }

    if (right_edge) {
      router_chip_index = -1;
      if (my_chip_x == MAX_BOARD_CONNECTIONS_X-1) {
        router_chip_index = get_y_router_chip_index(my_chip_x, my_chip_y);
      }
      wait_for_edge_connection_info(router_chip_index, min_eth_id_u, min_eth_id_d, routing_discovery_phase_t::PHASE_3);
      uint32_t worker_eth_id = min_eth_id_u == -1 ? 0 : min_eth_id_u;
      if (bot_edge && my_eth_id == worker_eth_id) {
        wait_for_phase(routing_discovery_phase_t::PHASE_3);
        populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_R);

        populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_D);
        signal_phase(routing_discovery_phase_t::PHASE_4);
      }
    }

    if (bot_edge) {
      router_chip_index = -1;
      if (my_chip_y == 0) {
        router_chip_index = get_x_router_chip_index(my_chip_x, my_chip_y);
      }
      wait_for_edge_connection_info(router_chip_index, min_eth_id_r, min_eth_id_l, routing_discovery_phase_t::PHASE_4);
      uint32_t worker_eth_id = min_eth_id_r == -1 ? 0 : min_eth_id_r;
      if (left_edge && my_eth_id == worker_eth_id) {
        wait_for_phase(routing_discovery_phase_t::PHASE_4);
        populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_D);

        signal_phase(routing_discovery_phase_t::PHASE_5);
      }
    }

    //second pass. Just do x = 0 column.
    //this time edge connection table shoudl be fully populated.
    if (left_edge) {
      relay_full_edge_connection_info(min_eth_id_d, min_eth_id_u, routing_discovery_phase_t::PHASE_5);
    }
  }
  //propagate rack connection info to all chips.
  //All chips with x=0 will send the info to their right.
  //i.e to all chips with x=1, 2 ... MAX_BOARD_CONNECTIONS_X
  relay_full_edge_connection_info(min_eth_id_l, min_eth_id_r, routing_discovery_phase_t::PHASE_5);

  //Wait for connection info to arrive from all the router chips.
  for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
    while (sys_connections->rack_router_chips[e] == ETH_UNKNOWN) {eth_link_status_check();}
  }

  init_routing_connections();

  uint32_t tag = my_chip_y;
  tag <<= NOC_ADDR_NODE_ID_BITS;
  tag |= my_chip_x;

  for (uint32_t i = 0; i < CMD_BUF_SIZE; i++)
  {
    eth_queues->req_cmd_q[eth_q_id_t::ROOT].cmd[i].src_addr_tag = tag;
    my_node_info->data_block_source[i] = -1;
  }
}




void eth_routing_init() {
  eth_queues = ((eth_queues_t *)ETH_ROUTING_STRUCT_ADDR);
  my_node_info_init();
}

void get_next_hop_sys_addr(uint32_t rack, uint64_t dest_addr, uint32_t &next_hop_rack, uint64_t &next_hop_sys_addr)
{

  uint32_t dest_chip_x = sys_addr_get_chip_x(dest_addr);
  uint32_t dest_chip_y = sys_addr_get_chip_y(dest_addr);
  uint32_t dest_rack_x = get_rack_x(rack);
  uint32_t dest_rack_y = get_rack_y(rack);
  uint32_t dest_eth_id = 0;
  uint32_t my_chip_x = my_node_info->my_chip_x;
  uint32_t my_chip_y = my_node_info->my_chip_y;
  uint32_t my_rack_x = my_node_info->my_rack_x;
  uint32_t my_rack_y = my_node_info->my_rack_y;
  bool rack_routing = false;
  bool next_hop_rack_x = false;
  bool rack_hop_route = false;

  if (!sys_addr_is_local_rack(rack, my_node_info)) {
    //get the chip x/y of the chip that connects to next shelf/rack
    for (int i = 0; i < MAX_EDGE_ROUTERS; i++) {
      dest_chip_x = get_chip_x_from_router_index(i);
      dest_chip_y = get_chip_y_from_router_index(i);
      dest_eth_id = (sys_connections->rack_router_chips[i] >> 4)&0xF;
      uint32_t conn_id = sys_connections->rack_router_chips[i] & 0xF;
      if (dest_rack_x < my_rack_x) {
        if (conn_id == ETH_RACK_L) {
          rack_routing = true;
          next_hop_rack_x = true;
          break;
        }
      } else if (dest_rack_x > my_rack_x) {
        if (conn_id == ETH_RACK_R) {
          rack_routing = true;
          next_hop_rack_x = true;
          break;
        }
      } else if (dest_rack_y < my_rack_y) {
        if (conn_id == ETH_RACK_D) {
          rack_routing = true;
          break;
        }
      } else if (dest_rack_y > my_rack_y) {
        if (conn_id == ETH_RACK_U) {
          rack_routing = true;
          break;
        }
      }
    }
    if (rack_routing == false) {
      //we do not have any link on current shelf to get to next rack/shelf.
      next_hop_sys_addr = -1;
      return;
    } else {
      if (dest_chip_x == my_chip_x && dest_chip_y == my_chip_y) {
        //we are at the chip that has a link to next rack/shelf
        rack_hop_route = true;
      }
    }
  }
  bool next_hop_x = (dest_chip_x != my_chip_x);
  bool next_hop_eth_link = false;

  if (my_node_info->routing_node) {

    uint32_t dest_eth_link_chip_x = sys_addr_get_chip_x(my_node_info->remote_eth_sys_addr);
    uint32_t dest_eth_link_chip_y = sys_addr_get_chip_y(my_node_info->remote_eth_sys_addr);

    if (sys_addr_is_local_rack(my_node_info->remote_eth_rack, my_node_info)) {
      if (next_hop_x) {
        if (dest_chip_x > my_chip_x) {
          next_hop_eth_link = dest_eth_link_chip_x > my_chip_x;
        } else if (dest_chip_x < my_chip_x){
          next_hop_eth_link = dest_eth_link_chip_x < my_chip_x;
        }
      } else {
        if (dest_chip_y > my_chip_y) {
          next_hop_eth_link = dest_eth_link_chip_y > my_chip_y;
        } else if (dest_chip_y < my_node_info->my_chip_y){
          next_hop_eth_link = dest_eth_link_chip_y < my_chip_y;
        }
      }
    }
    if (rack_routing) {
    //we need to to go a different rack/shelf
      if (dest_chip_x == my_chip_x && dest_chip_y == my_chip_y) {
        //we are at the chip that has a link to next rack/shelf
        if (my_node_info->my_eth_id == dest_eth_id) {
          //we are at an ethernet node that is connected to next rack/shelf
          next_hop_eth_link = true;
        }
      }
    }
  }
  if (next_hop_eth_link) {
    next_hop_sys_addr = my_node_info->remote_eth_sys_addr;
    next_hop_rack = my_node_info->remote_eth_rack;
  } else {
    next_hop_rack = get_sys_rack(my_rack_x, my_rack_y);
    uint32_t next_hop_sys_addr_local = 0;
    if (rack_hop_route) {
      //we are at node connected to local chip.
      //need a noc hop to ethernet node connected to next rack/shelf
      if (next_hop_rack_x) {
        next_hop_sys_addr_local = my_node_info->rack_route_x_sys_addr;
      } else {
        next_hop_sys_addr_local = my_node_info->rack_route_y_sys_addr;
      }
    } else {
      if (next_hop_x) {
        if (dest_chip_x > my_chip_x) {
          next_hop_sys_addr_local = my_node_info->dest_x_route_r_sys_addr;
        } else {
          next_hop_sys_addr_local = my_node_info->dest_x_route_l_sys_addr;
        }
      } else {
        if (dest_chip_y > my_chip_y) {
          next_hop_sys_addr_local = my_node_info->dest_y_route_u_sys_addr;
        } else {
          next_hop_sys_addr_local = my_node_info->dest_y_route_d_sys_addr;
        }
      }
    }
    if ((next_hop_sys_addr_local == 0xFFFFFFFF) || (next_hop_sys_addr_local == get_sys_addr_tag(my_chip_x, my_chip_y, eth_id_get_x(my_node_info->my_eth_id), eth_id_get_y(my_node_info->my_eth_id)))) {
      //if next hop address is the same as current node address, it means destination is unreachable.
      //This address is only possible if there is rack_routing in progress.
      next_hop_sys_addr = -1;
    } else {
      next_hop_sys_addr = (uint64_t)next_hop_sys_addr_local << NOC_ADDR_LOCAL_BITS;
    }
  }
}

void cmd_service_error(routing_cmd_t* req_cmd, routing_cmd_t* resp_cmd) {
  if (req_cmd->flags & CMD_WR_REQ) {
    resp_cmd->flags = CMD_WR_ACK;
  } else if (req_cmd->flags & CMD_RD_REQ) {
    resp_cmd->flags = CMD_RD_DATA;
  }
  if (req_cmd->flags & CMD_DATA_BLOCK) {
    resp_cmd->flags |= CMD_DATA_BLOCK;
  }
  resp_cmd->flags |= CMD_DEST_UNREACHABLE;
}

int32_t data_block_reserve(uint32_t source_addr)
{
  for (uint32_t i = 0; i < CMD_BUF_SIZE; i++) {
    if ((uint32_t)my_node_info->data_block_source[i] == source_addr) {
      return i;
    }
  }
  for (uint32_t i = 0; i < CMD_BUF_SIZE; i++) {
    if (my_node_info->data_block_source[i] == -1) {
      my_node_info->data_block_source[i] = source_addr;
      return i;
    }
  }
  return -1;
}

int32_t get_block_buf(uint32_t source_addr)
{
  for (uint32_t i = 0; i < CMD_BUF_SIZE; i++) {
    if ((uint32_t)my_node_info->data_block_source[i] == source_addr) {
      return (int32_t) i;
    }
  }
  return -1;
}

void cmd_service_local(routing_cmd_t* req_cmd, routing_cmd_t* resp_cmd) {
  uint32_t* req_addr = (uint32_t*)(sys_addr_get_offset(req_cmd->sys_addr));
  if (req_cmd->flags == CMD_DATA_BLOCK_RESERVE)
  {
    int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);
    if (buf_id != -1) {
      resp_cmd->data = buf_id; // used as next hop buff in previous hop
      resp_cmd->flags = CMD_DATA_BLOCK_RESERVE_ACK;
    } else {
      resp_cmd->flags = CMD_DATA_BLOCK_UNAVAILABLE;
    }
  }
  else if (req_cmd->flags == CMD_DATA_BLOCK_FREE)
  {
    resp_cmd->flags = CMD_DATA_BLOCK_FREE_ACK;
  } else if (req_cmd->flags & CMD_DATA_BLOCK) {
    uint32_t num_words = req_cmd->data >> 2;
    int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);
    if (buf_id != -1) {
      if (req_cmd->flags & CMD_WR_REQ) {
        volatile uint32_t* req_data = (volatile uint32_t *)(ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < num_words; i++) {
          req_addr[i] = req_data[i];
        }
        resp_cmd->flags = CMD_WR_ACK | CMD_DATA_BLOCK;
      } else {
        volatile uint32_t* req_data = (volatile uint32_t *)(ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE);
        for (uint32_t i = 0; i < num_words; i++) {
          req_data[i] = req_addr[i];
        }
        resp_cmd->data = req_cmd->data; // return the number of bytes being sent back to requestor.
        resp_cmd->flags = CMD_RD_DATA | CMD_DATA_BLOCK;
      }
    } else {
      resp_cmd->flags = CMD_DATA_BLOCK_UNAVAILABLE;
    }
  } else {
    if (req_cmd->flags & CMD_WR_REQ) {
      req_addr[0] = req_cmd->data;
      resp_cmd->flags = CMD_WR_ACK;
    } else {
      resp_cmd->data = req_addr[0];
      resp_cmd->flags = CMD_RD_DATA;
    }
  }
}

void cmd_service_noc(routing_cmd_t* req_cmd, routing_cmd_t* resp_cmd) {

  uint32_t dest_noc_x = sys_addr_get_noc_x(req_cmd->sys_addr);
  uint32_t dest_noc_y = sys_addr_get_noc_y(req_cmd->sys_addr);
  uint64_t dest_offset = sys_addr_get_offset(req_cmd->sys_addr);
  uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;
  uint32_t alignment_offset = (dest_offset >> 2) & 0x7; //32-byte alignment

  if (req_cmd->flags & CMD_DATA_BLOCK_RESERVE)
  {
    int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);
    if (buf_id != -1) {
      resp_cmd->data = buf_id; // used as next hop buff in previous hop
      resp_cmd->flags = CMD_DATA_BLOCK_RESERVE_ACK;
    } else {
      resp_cmd->flags = CMD_DATA_BLOCK_UNAVAILABLE;
    }
  }
  else if (req_cmd->flags == CMD_DATA_BLOCK_FREE)
  {
    resp_cmd->flags = CMD_DATA_BLOCK_FREE_ACK;
  } else if (req_cmd->flags & CMD_DATA_BLOCK) {

    uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
    int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);
    uint32_t data_buf = ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
    if (buf_id != -1) {
      if (req_cmd->flags & CMD_WR_REQ) {
        while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
        ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                            data_buf,
                            NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset),
                            num_bytes,
                            ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
        resp_cmd->flags = CMD_WR_ACK | CMD_DATA_BLOCK;
      } else {
        while (!ncrisc_noc_fast_read_ok(noc_id, NCRISC_SMALL_TXN_CMD_BUF));
        ncrisc_noc_fast_read(noc_id, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset), data_buf, num_bytes, NCRISC_RD_DEF_TRID);
        while(!ncrisc_noc_reads_flushed(noc_id, NCRISC_RD_DEF_TRID));
        resp_cmd->data = req_cmd->data; // return the number of bytes being sent back to requestor.
        resp_cmd->flags = CMD_RD_DATA | CMD_DATA_BLOCK;
      }
    } else {
      resp_cmd->flags = CMD_DATA_BLOCK_UNAVAILABLE;
    }
  } else {
    if (req_cmd->flags & CMD_WR_REQ) {
      noc_scratch_buf[alignment_offset] = req_cmd->data;
      while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
      ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                          (uint32_t)noc_scratch_buf + alignment_offset * 4,
                          NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset),
                          4,
                          ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
      while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
      resp_cmd->flags = CMD_WR_ACK;
    } else {
      while (!ncrisc_noc_fast_read_ok(noc_id, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_read(noc_id, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 4, NCRISC_RD_DEF_TRID);
      while(!ncrisc_noc_reads_flushed(noc_id, NCRISC_RD_DEF_TRID));
      resp_cmd->flags = CMD_RD_DATA;
      resp_cmd->data = noc_scratch_buf[alignment_offset];
      noc_scratch_buf[7] = resp_cmd->data;
      //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, resp_cmd->data);
    }
  }
}

void req_fwd_noc(eth_q_id_t q_id, routing_cmd_t* req_cmd, uint64_t next_hop_sys_addr) {

  routing_cmd_t* fwd_cmd_ptr = (routing_cmd_t*)noc_scratch_buf;

  uint32_t dest_index = sys_addr_get_erisc_dest_index(next_hop_sys_addr);
  uint32_t dest_wr_index = eth_queues->dest_wr_ptr[q_id != eth_q_id_t::ROOT][dest_index].ptr & CMD_BUF_SIZE_MASK;
  uint32_t dest_noc_x = sys_addr_get_noc_x(next_hop_sys_addr);
  uint32_t dest_noc_y = sys_addr_get_noc_y(next_hop_sys_addr);
  uint32_t dest_addr = (uint32_t)(&(eth_queues->req_cmd_q[q_id].cmd[dest_wr_index]));
  int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);

  if (req_cmd->flags == (CMD_DATA_BLOCK | CMD_WR_REQ))
  {
    uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
    uint32_t src_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + my_node_info->next_hop_buf_index[buf_id] * MAX_BLOCK_SIZE;
    while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        src_buf_addr,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_buf_addr),
                        num_bytes,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);

  }

  *fwd_cmd_ptr = *req_cmd;

  while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF)){}
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        (uint32_t)noc_scratch_buf,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_addr),
                        CMD_SIZE_BYTES,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
}

void resp_fwd_noc(eth_q_id_t q_id, routing_cmd_t* resp_cmd) {

  routing_cmd_t* fwd_cmd_ptr = (routing_cmd_t*)noc_scratch_buf;

  uint32_t dest_wr_index = resp_cmd->src_resp_buf_index;
  uint32_t dest_noc_x = sys_addr_get_noc_x(my_node_info->src_sys_addr[q_id]);
  uint32_t dest_noc_y = sys_addr_get_noc_y(my_node_info->src_sys_addr[q_id]);
  uint32_t dest_addr = (uint32_t)(&(eth_queues->resp_cmd_q[q_id].cmd[dest_wr_index]));
  int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);

  if (!cmd_error(resp_cmd) && resp_cmd->flags == (CMD_DATA_BLOCK | CMD_RD_DATA))
  {
    uint32_t num_bytes = resp_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
    uint32_t src_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + my_node_info->prev_hop_buf_index[buf_id] * MAX_BLOCK_SIZE;
    while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        src_buf_addr,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_buf_addr),
                        num_bytes,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  } else if (resp_cmd->flags == CMD_DATA_BLOCK_RESERVE_ACK) {
    int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);
    int32_t buf_id_previous = my_node_info->prev_hop_buf_index[buf_id];
    my_node_info->next_hop_buf_index[buf_id] = resp_cmd->data;
    resp_cmd->data = buf_id;
    // In case response is going to root node host Q,
    // we need to update next_hop_buf_index[buf_id_previous] in root_node::my_node_info
    if (q_id == eth_q_id_t::ROOT) {
      uint32_t addr = (uint32_t)(&(my_node_info->next_hop_buf_index[buf_id_previous]));
      noc_write_aligned_dw(dest_noc_x, dest_noc_y, addr, buf_id);
      resp_cmd->data = buf_id_previous;
    }

  } else if (resp_cmd->flags & (CMD_DATA_BLOCK_UNAVAILABLE | CMD_DEST_UNREACHABLE)) {
    int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);
    int32_t buf_id_previous;
    if (buf_id != -1) {
      // current node has a buffer reserved. Reservation error is coming from a downstream node.
      // unreserve current node's data buffer.
      my_node_info->data_block_source[buf_id] = -1;
      buf_id_previous = my_node_info->prev_hop_buf_index[buf_id];
    } else {
      // in this case reservation error is coming from current node.
      // current node does not have free buffer.
      // previous node's reserved buffer id is in resp_cmd->data.
      buf_id_previous = resp_cmd->data;
    }
    if (q_id == eth_q_id_t::ROOT) {
      //clear buffer reservation in root node since this response is being
      //forwarded to root node's root resp queue.
      uint32_t addr = (uint32_t)(&(my_node_info->data_block_source[buf_id_previous]));
      noc_write_aligned_dw(dest_noc_x, dest_noc_y, addr, -1);
    }

  }

  *fwd_cmd_ptr = *resp_cmd;

  while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF)){}
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        (uint32_t)noc_scratch_buf,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_addr),
                        CMD_SIZE_BYTES/2,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
}

void req_fwd_eth(routing_cmd_t* req_cmd) {

  uint32_t eth_wr_index = eth_queues->eth_out_req_cmd_q.wrptr.ptr & CMD_BUF_SIZE_MASK;
  int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);

  if (req_cmd->flags == (CMD_DATA_BLOCK | CMD_WR_REQ))
  {
    uint32_t num_words = ((req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4))) + 0xF ) >> 4;
    uint32_t src_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + my_node_info->next_hop_buf_index[buf_id] * MAX_BLOCK_SIZE;
    eth_send_packet(0, src_buf_addr>>4, dest_buf_addr>>4, num_words);
  }

  eth_queues->eth_out_req_cmd_q.cmd[eth_wr_index] = *req_cmd;
  cmd_q_advance_wrptr(&eth_queues->eth_out_req_cmd_q);

  uint32_t src_addr = (uint32_t)(&(eth_queues->eth_out_req_cmd_q.cmd[eth_wr_index]));
  uint32_t dest_addr = (uint32_t)(&(eth_queues->eth_in_req_cmd_q.cmd[eth_wr_index]));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, CMD_SIZE_BYTES/16);

  src_addr = (uint32_t)(&(eth_queues->eth_out_req_cmd_q.wrptr));
  dest_addr = (uint32_t)(&(eth_queues->eth_in_req_cmd_q.wrptr));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
}


void resp_fwd_eth(routing_cmd_t* resp_cmd) {

  uint32_t eth_wr_index = eth_queues->eth_out_resp_cmd_q.wrptr.ptr & CMD_BUF_SIZE_MASK;
  int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);

  if (!cmd_error(resp_cmd) && resp_cmd->flags == (CMD_DATA_BLOCK | CMD_RD_DATA))
  {
    uint32_t num_words = ((resp_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4))) + 0xF ) >> 4;
    uint32_t src_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + my_node_info->prev_hop_buf_index[buf_id] * MAX_BLOCK_SIZE;
    eth_send_packet(0, src_buf_addr>>4, dest_buf_addr>>4, num_words);
  } else if (resp_cmd->flags == CMD_DATA_BLOCK_RESERVE_ACK) {
    int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);
    my_node_info->next_hop_buf_index[buf_id] = resp_cmd->data;
    resp_cmd->data = buf_id;
  } else if (resp_cmd->flags & (CMD_DATA_BLOCK_UNAVAILABLE | CMD_DEST_UNREACHABLE)) {
    // A downstream node failed to reserve buffer or destination node is unreachable
    // unreserve the buffer on current node.
    int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);
    if (buf_id != -1) {
      my_node_info->data_block_source[buf_id] = -1;
    }
  }

  eth_queues->eth_out_resp_cmd_q.cmd[eth_wr_index] = *resp_cmd;
  cmd_q_advance_wrptr(&eth_queues->eth_out_resp_cmd_q);

  uint32_t src_addr = (uint32_t)(&(eth_queues->eth_out_resp_cmd_q.cmd[eth_wr_index]));
  uint32_t dest_addr = (uint32_t)(&(eth_queues->eth_in_resp_cmd_q.cmd[eth_wr_index]));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, CMD_SIZE_BYTES/16);

  src_addr = (uint32_t)(&(eth_queues->eth_out_resp_cmd_q.wrptr));
  dest_addr = (uint32_t)(&(eth_queues->eth_in_resp_cmd_q.wrptr));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
}


bool dest_req_cmd_q_full(eth_q_id_t q_id, uint32_t dest_index) {
  return cmd_q_ptrs_full(eth_queues->dest_wr_ptr[q_id != eth_q_id_t::ROOT][dest_index].ptr, eth_queues->dest_rd_ptr[q_id != eth_q_id_t::ROOT][dest_index].ptr);
}


bool dest_req_cmd_q_empty(eth_q_id_t q_id, uint32_t dest_index) {
  return cmd_q_ptrs_empty(eth_queues->dest_wr_ptr[q_id != eth_q_id_t::ROOT][dest_index].ptr, eth_queues->dest_rd_ptr[q_id != eth_q_id_t::ROOT][dest_index].ptr);
}


void noc_write_aligned_dw(uint32_t dest_noc_x, uint32_t dest_noc_y, uint32_t addr, uint32_t val) {
  uint32_t align_index = (addr % 32) / 4;
  noc_scratch_buf[align_index] = val;
  /*
  uint32_t wr_sent = noc_status_reg(NIU_MST_POSTED_WR_REQ_STARTED);
  noc_copy(NOC_XY_ADDR(my_node_info->my_noc_x, my_node_info->my_noc_y, ((uint32_t)noc_scratch_buf) + 4*align_index),
           NOC_XY_ADDR(dest_noc_x, dest_noc_y, addr),
           4, false, true, true, 0, 0, 0);
  wr_sent++;
  while (noc_status_reg(NIU_MST_POSTED_WR_REQ_SENT) != wr_sent);
  */
  while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF)){}
  ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        ((uint32_t)noc_scratch_buf) + 4*align_index,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, addr),
                        4,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
}


void dest_req_cmd_q_advance_wrptr(eth_q_id_t q_id, uint64_t next_hop_sys_addr) {

  uint32_t dest_index = sys_addr_get_erisc_dest_index(next_hop_sys_addr);
  remote_update_ptr_t* dest_wr_ptr = eth_queues->dest_wr_ptr[q_id != eth_q_id_t::ROOT];
  remote_update_ptr_advance(dest_wr_ptr + dest_index);

  uint32_t dest_noc_x = sys_addr_get_noc_x(next_hop_sys_addr);
  uint32_t dest_noc_y = sys_addr_get_noc_y(next_hop_sys_addr);
  uint32_t req_cmd_q_wrptr_addr = (uint32_t)(&(eth_queues->req_cmd_q[q_id].wrptr.ptr));
  uint32_t dest_wr_ptr_val = dest_wr_ptr[dest_index].ptr;
  noc_write_aligned_dw(dest_noc_x, dest_noc_y, req_cmd_q_wrptr_addr, dest_wr_ptr_val);
}


void src_update_req_q_rdptr(eth_q_id_t q_id) {

  cmd_q_t* req_cmd_q = &eth_queues->req_cmd_q[q_id];
  uint32_t req_cmd_q_rdptr_val = req_cmd_q->rdptr.ptr;
  uint64_t src_sys_addr = my_node_info->src_sys_addr[q_id];
  uint32_t src_rack = my_node_info->src_rack[q_id];
  uint32_t src_req_cmd_q_rdptr_val =  my_node_info->src_rdptr_val[q_id];

  if (sys_addr_is_local_chip(src_rack, src_sys_addr, my_node_info)) {

    if (req_cmd_q_rdptr_val != src_req_cmd_q_rdptr_val) {
      remote_update_ptr_t* dest_rd_ptr = eth_queues->dest_rd_ptr[q_id != eth_q_id_t::ROOT];
      uint32_t src_req_q_rdptr_addr = (uint32_t)(dest_rd_ptr + my_node_info->src_dest_index);
      noc_write_aligned_dw(sys_addr_get_noc_x(src_sys_addr),
                           sys_addr_get_noc_y(src_sys_addr),
                           src_req_q_rdptr_addr, req_cmd_q_rdptr_val);
      my_node_info->src_rdptr_val[q_id] = req_cmd_q_rdptr_val;
    }
  }
}


void eth_in_req_cmd_q_rdptr_update() {
  uint32_t src_addr = (uint32_t)(&(eth_queues->eth_in_req_cmd_q.rdptr));
  uint32_t dest_addr = (uint32_t)(&(eth_queues->eth_out_req_cmd_q.rdptr));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
}


void eth_in_resp_cmd_q_rdptr_update() {
  uint32_t src_addr = (uint32_t)(&(eth_queues->eth_in_resp_cmd_q.rdptr));
  uint32_t dest_addr = (uint32_t)(&(eth_queues->eth_out_resp_cmd_q.rdptr));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
}


void eth_routing_handler() {

  // check if request queue empty
  for (int i = 0; i < eth_q_id_t::NUM_Q; i++) {
    cmd_q_t* req_cmd_q = &eth_queues->req_cmd_q[i];
    cmd_q_t* resp_cmd_q = &eth_queues->resp_cmd_q[i];
    if (!cmd_q_is_empty(req_cmd_q) && !cmd_q_is_full(resp_cmd_q)) {

      test_results[8]++;

      uint32_t req_cmd_q_index = req_cmd_q->rdptr.ptr & CMD_BUF_SIZE_MASK;
      uint32_t resp_cmd_q_index = resp_cmd_q->wrptr.ptr & CMD_BUF_SIZE_MASK;
      routing_cmd_t* req_cmd = req_cmd_q->cmd + req_cmd_q_index;
      routing_cmd_t* resp_cmd = resp_cmd_q->cmd + resp_cmd_q_index;

      resp_cmd->flags = 0;
      resp_cmd->src_resp_q_id = req_cmd->src_resp_q_id;
      resp_cmd->src_resp_buf_index = req_cmd->src_resp_buf_index;
      resp_cmd->src_addr_tag = req_cmd->src_addr_tag;
      req_cmd->src_resp_buf_index = resp_cmd_q_index;
      req_cmd->src_resp_q_id = i;

      if (req_cmd->flags & CMD_DATA_BLOCK)
      {
        // if data block mode, data field contians number of bytes to move.
        // max size = MAX_BLOCK_SIZE
        if (req_cmd->data > MAX_BLOCK_SIZE)
        {
          req_cmd->data = MAX_BLOCK_SIZE;
        }
        //check if block read/write command is received without
        //a reserved buffer.
        int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);
        if (buf_id == -1) {
          resp_cmd->flags = CMD_DATA_BLOCK_UNAVAILABLE;
        }
      }

      if (req_cmd->flags == CMD_DATA_BLOCK_RESERVE)
      {
        int32_t buf_id = data_block_reserve(req_cmd->src_addr_tag);
        if (buf_id != -1) {
          my_node_info->prev_hop_buf_index[buf_id] = req_cmd->data; // don't care for root node.
          req_cmd->data = buf_id; // used as previos hop buff in next hop
        } else {
          //current node cannot reserve data buffer
          resp_cmd->data = req_cmd->data; // return previous hop buffer id so that it can be unreserved.
          resp_cmd->flags = CMD_DATA_BLOCK_UNAVAILABLE;
        }
      }
      else if (req_cmd->flags == CMD_DATA_BLOCK_FREE)
      {
        int32_t buf_id = get_block_buf(req_cmd->src_addr_tag);
        if (buf_id != -1) {
          my_node_info->data_block_source[buf_id] = -1;
        }
      }

      uint64_t cmd_dest_sys_addr = req_cmd->sys_addr;
      uint32_t rack = req_cmd->rack;
      uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;

      if (sys_addr_is_local(noc_id, rack, cmd_dest_sys_addr, my_node_info)) {
        // perform local read/write and store the response into the resp queue, no forwarding
        test_results[9]++;
        cmd_service_local(req_cmd, resp_cmd);
        cmd_q_advance_wrptr(resp_cmd_q);
        cmd_q_advance_rdptr(req_cmd_q);
      } else {
        if (sys_addr_is_local_chip(rack, cmd_dest_sys_addr, my_node_info)) {
          //do a noc copy for local chip access
          cmd_service_noc(req_cmd, resp_cmd);
          cmd_q_advance_wrptr(resp_cmd_q);
          cmd_q_advance_rdptr(req_cmd_q);
        } else {
          //need to route over ethernet.
          //figure out which ethernet core to forward this request to.
          uint64_t next_hop_sys_addr = 0;
          uint32_t next_hop_rack = 0;
          get_next_hop_sys_addr(rack, cmd_dest_sys_addr, next_hop_rack, next_hop_sys_addr);

          //either block cannot be reserved by intermediate node or
          //data block request is received without reserving a data buffer on intermediate node.
          if (((req_cmd->flags == CMD_DATA_BLOCK_RESERVE) || (req_cmd->flags == CMD_DATA_BLOCK)) && (resp_cmd->flags == CMD_DATA_BLOCK_UNAVAILABLE)) {
            cmd_q_advance_wrptr(resp_cmd_q);
            cmd_q_advance_rdptr(req_cmd_q);
          } else if ((int64_t) next_hop_sys_addr == -1) {
            //destination is unreachable. return error.
            cmd_service_error(req_cmd, resp_cmd);
            cmd_q_advance_wrptr(resp_cmd_q);
            cmd_q_advance_rdptr(req_cmd_q);
          } else {
            if (sys_addr_is_local_chip(next_hop_rack, next_hop_sys_addr, my_node_info)) {
              uint32_t dest_index = sys_addr_get_erisc_dest_index(next_hop_sys_addr);
              if (!dest_req_cmd_q_full((eth_q_id_t)i, dest_index)) {
                test_results[10]++;
                req_fwd_noc((eth_q_id_t)i, req_cmd, next_hop_sys_addr);
                cmd_q_advance_wrptr(resp_cmd_q);
                cmd_q_advance_rdptr(req_cmd_q);

                //Update local coopy of next hop wr ptr.
                //Increment next hop req_cmd_q write ptr.
                dest_req_cmd_q_advance_wrptr((eth_q_id_t)i, next_hop_sys_addr);
              }
            }
            else {
              if (!cmd_q_is_full(&eth_queues->eth_out_req_cmd_q)) {
                test_results[11]++;
                req_fwd_eth(req_cmd);
                cmd_q_advance_wrptr(resp_cmd_q);
                cmd_q_advance_rdptr(req_cmd_q);
              }
            }
          }
        }
      }
    }
    //update the read pointer in the node that is writing
    //to my req_cmd_q
    src_update_req_q_rdptr((eth_q_id_t)i);
  }

  if (my_node_info->routing_node) {
    for (int i = my_node_info->root_node; i < eth_q_id_t::NUM_Q; i++) {
      cmd_q_t* resp_cmd_q = &eth_queues->resp_cmd_q[i];
      if (!cmd_q_is_empty(resp_cmd_q)) {
        uint32_t resp_cmd_q_rd_index = resp_cmd_q->rdptr.ptr & CMD_BUF_SIZE_MASK;
        routing_cmd_t* resp_cmd = resp_cmd_q->cmd + resp_cmd_q_rd_index;
        if (resp_cmd->flags != 0) {
          if (sys_addr_is_local_chip(my_node_info->src_rack[i], my_node_info->src_sys_addr[i], my_node_info)) {
            test_results[12]++;
            resp_fwd_noc((eth_q_id_t)i, resp_cmd);
            cmd_q_advance_rdptr(resp_cmd_q);
          } else if (!cmd_q_is_full(&eth_queues->eth_out_resp_cmd_q)) {
            test_results[13]++;
            resp_fwd_eth(resp_cmd);
            cmd_q_advance_rdptr(resp_cmd_q);
          }
        }
      }
    }
  }
  // Request is coming in over ethernet link from neighboring chip.
  if (my_node_info->routing_node) {
    cmd_q_t* eth_req_cmd_q = &eth_queues->req_cmd_q[my_node_info->eth_req_q_id];
    if (!cmd_q_is_empty(&eth_queues->eth_in_req_cmd_q) && !cmd_q_is_full(eth_req_cmd_q)) {
      uint32_t eth_in_req_cmd_q_rd_index = eth_queues->eth_in_req_cmd_q.rdptr.ptr & CMD_BUF_SIZE_MASK;
      uint32_t req_cmd_q_wr_index = eth_req_cmd_q->wrptr.ptr & CMD_BUF_SIZE_MASK;
      eth_req_cmd_q->cmd[req_cmd_q_wr_index] = eth_queues->eth_in_req_cmd_q.cmd[eth_in_req_cmd_q_rd_index];
      cmd_q_advance_wrptr(eth_req_cmd_q);
      cmd_q_advance_rdptr(&eth_queues->eth_in_req_cmd_q);
      eth_in_req_cmd_q_rdptr_update();
    }
  }

  // Response is coming in on an etherlink from neighboring chip.
  if (my_node_info->routing_node) {
    if (!cmd_q_is_empty(&eth_queues->eth_in_resp_cmd_q)) {
      uint32_t eth_in_resp_cmd_q_rd_index = eth_queues->eth_in_resp_cmd_q.rdptr.ptr & CMD_BUF_SIZE_MASK;
      routing_cmd_t* resp_cmd = eth_queues->eth_in_resp_cmd_q.cmd + eth_in_resp_cmd_q_rd_index;
      uint32_t resp_wr_index = resp_cmd->src_resp_buf_index;
      eth_q_id_t resp_q_id = (eth_q_id_t)resp_cmd->src_resp_q_id;

      if (my_node_info->root_node && resp_q_id == eth_q_id_t::ROOT) {
        if (resp_cmd->flags == CMD_DATA_BLOCK_RESERVE_ACK) {
          int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);
          my_node_info->next_hop_buf_index[buf_id] = resp_cmd->data;
          resp_cmd->data = buf_id;
        }
        else if (resp_cmd->flags & (CMD_DATA_BLOCK_UNAVAILABLE | CMD_DEST_UNREACHABLE)) {
          // A downstream node failed to reserve buffer or the destination node is unreachable
          // unreserve the buffer on current node.
          int32_t buf_id = get_block_buf(resp_cmd->src_addr_tag);
          if (buf_id != -1) {
            my_node_info->data_block_source[buf_id] = -1;
          }
        }
      }

      eth_queues->resp_cmd_q[resp_q_id].cmd[resp_wr_index].data = resp_cmd->data;
      eth_queues->resp_cmd_q[resp_q_id].cmd[resp_wr_index].flags = resp_cmd->flags;
      cmd_q_advance_rdptr(&eth_queues->eth_in_resp_cmd_q);
      eth_in_resp_cmd_q_rdptr_update();
    }
  }

}

void eth_link_status_check(void) {
  if ((!my_node_info->eth_node) || (my_node_info->train_status != LINK_TRAIN_SUCCESS)) return;

  uint32_t pcs_status = eth_pcs_ind_reg_read(0x30001); // rx link up, should get 0x6
  uint32_t pcs_faults = eth_pcs_ind_reg_read(0x30008);
  uint32_t crc_err = eth_mac_reg_read(0x928);
  test_results[5] = pcs_status;
  test_results[47] = crc_err;
  test_results[49] = pcs_faults;

  if (!config_buf->link_status_check_en) return;

  bool restart_link = false;

  uint64_t curr_time = eth_read_wall_clock();

  if (curr_time > next_timestamp) {
    uint32_t mac_frames = eth_mac_reg_read(0x900);

    if (test_results[6] == mac_frames) {
      mac_rcv_fail++;
      test_results[14] = mac_rcv_fail;
      test_results[6] = mac_frames;
    } else {
      test_results[6] = mac_frames;
    }

    init_timestamps();
  }

  if (pcs_status != 0x6) {
    // test_results[5] = pcs_status;
    restart_link = true;
    test_results[50]++;
  } else if (mac_rcv_fail > config_buf->mac_rcv_fail_limit) {
    restart_link = true;
  } else if (crc_err > 0) {
    test_results[51]++;
    restart_link = true;
  } else {}

  // FIXME chicken bit to force reset PCS, move to command table later
  if (test_results[15] == 1) restart_link = true;

  if (restart_link) {
    link_train_status_e train_status;
    do {
      for (uint32_t i = 0; i < (DEBUG_BUF_SIZE)/4; i++) {
        debug_buf[i] = 0;
      }

      // Clear dummy packet handshake data
      for (uint32_t i = 16; i < 24; i++) {
        test_results[i] = 0;
      }

      train_status = eth_link_start_handler(config_buf, LINK_PCS_RESET, true);
      my_node_info->train_status = train_status;
      test_results[7]++;
    } while (my_node_info->train_status != LINK_TRAIN_SUCCESS);

    mac_rcv_fail = 0;
    test_results[5] = 0;
    test_results[6] = 0;
    // test_results[14] = mac_rcv_fail;
    test_results[15] = 0;
  }
}
