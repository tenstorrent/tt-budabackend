// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>

#include "eth_routing_v2.h"
#include "eth_init.h"
#include "eth_ss.h"
#include "noc.h"
#include "noc_nonblocking_api.h"
#include "host_mem_address_map.h"

extern uint32_t CQ_MAILBOX;
static eth_queues_t *eth_queues;
static uint32_t mac_rcv_fail;
static uint64_t next_timestamp;
uint64_t routing_cmd_timestamp[NODE_CMD_BUF_SIZE];
volatile uint32_t noc_scratch_buf[8] __attribute__((aligned(32)));

uint32_t remote_chip_id_src[4]  __attribute__((aligned(16)));
volatile uint32_t remote_chip_id_dest[4] __attribute__((aligned(16)));
uint32_t remote_board_type;

volatile uint32_t discovery_req_out[4] __attribute__((aligned(16)));
volatile uint32_t discovery_req_in[4] __attribute__((aligned(16)));
volatile uint32_t discovery_resp_out[4] __attribute__((aligned(16)));
volatile uint32_t discovery_resp_in[4] __attribute__((aligned(16)));

volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
volatile eth_conn_info_t* const eth_conn_info = (volatile eth_conn_info_t*)(ETH_CONN_INFO_ADDR);
volatile system_connection_t* sys_connections = (volatile system_connection_t*)(ETH_CONN_INFO_ADDR);
volatile uint32_t * rack_router_chips = (volatile uint32_t *)(ETH_ROUTING_DATA_BUFFER_ADDR); // Temp buffer to use during shelf discovery.
volatile uint32_t * rack_to_rack_router_chips = (volatile uint32_t *)(ETH_ROUTING_DATA_BUFFER_ADDR + (MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2) * 4); // Temp buffer to use during rack discovery.
volatile uint32_t * broadcast_header = (volatile uint32_t *)(ETH_ROUTING_STRUCT_ADDR + sizeof (eth_queues_t));
volatile uint32_t conn_info_sent;
volatile uint32_t conn_info_rcvd;
volatile uint32_t rack_conn_info_sent[3];
volatile uint32_t rack_conn_info_rcvd[3];
volatile uint8_t remote_eth_ids[NUM_ETH_INST];
uint8_t links_per_dir[eth_route_direction_t::COUNT];

node_info_t* const my_node_info = (node_info_t*)(NODE_INFO_ADDR);
volatile uint32_t* const test_results = (volatile uint32_t*)(RESULTS_BUF_ADDR);
volatile uint32_t* const debug_buf = (volatile uint32_t*)(DEBUG_BUF_ADDR);
volatile uint32_t* const tmp_2k_buf = (volatile uint32_t*)(TEMP_2K_BUF_ADDR);

#define ROUTING_VC 0
#define ROUTING_NOC 0
#define ENABLE_TIMESTAMPING 1

#define LAST_ORDERED_DRAM_BLOCK_WRITE (CMD_LAST_DATA_BLOCK_DRAM | CMD_ORDERED | CMD_DATA_BLOCK_DRAM | CMD_DATA_BLOCK | CMD_WR_REQ)
#define ORDERED_DRAM_BLOCK_WRITE (CMD_ORDERED | CMD_DATA_BLOCK_DRAM | CMD_DATA_BLOCK | CMD_WR_REQ)
#define LAST_ORDERED_DRAM_BLOCK_WRITE_ACK (CMD_LAST_DATA_BLOCK_DRAM | CMD_ORDERED | CMD_DATA_BLOCK_DRAM | CMD_DATA_BLOCK | CMD_WR_ACK)
#define ORDERED_DRAM_BLOCK_WRITE_ACK (CMD_ORDERED | CMD_DATA_BLOCK_DRAM | CMD_DATA_BLOCK | CMD_WR_ACK)

void init_timestamps() {
  uint32_t aiclk_ps = config_buf->aiclk_ps;
  if (aiclk_ps == 0) aiclk_ps = 1;
  uint8_t clk_scalar = BOOT_REFCLK_PERIOD_PS / (aiclk_ps);

  uint32_t wait_cycles = 27000 * clk_scalar * 100; //~1s
  uint64_t curr_timestamp = eth_read_wall_clock();
  next_timestamp = curr_timestamp + wait_cycles;
}

void copy_eth_cmd(uint32_t src, uint32_t dest, uint32_t num_words)
{
  volatile uint32_t *psrc = (volatile uint32_t *)src;
  volatile uint32_t *pdest = (volatile uint32_t *)dest;
  for (uint32_t i = 0; i < num_words; i++) {
    pdest[i] = psrc[i];
  }
}

void wait_write_flush(uint32_t noc_id, uint32_t txn_id, uint32_t post_code)
{
  volatile uint32_t * debug = (volatile uint32_t *)0x9010;
  uint16_t count = 0;
  while (!ncrisc_noc_nonposted_writes_flushed(noc_id, txn_id)) {
    count++;
    debug[0] = post_code | count;
  }
}
void eth_link_init() {
    //eth_ctrl_reg_write(ETH_CTRL_SCRATCH0, 1);

    eth_rxq_init(0, 0, 0, false, true);

    for (int i = 0; i < 4; i++) {
      remote_chip_id_dest[i] = 0;
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
    remote_board_type = test_results[72];

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

//uint32_t __attribute__((section(".misc_init"))) get_shelf_route_tag(eth_route_direction_t direction) {
uint32_t get_shelf_route_tag(uint32_t static_route, eth_route_direction_t direction) {
  uint32_t tag = -1;
  bool route_available = my_node_info->eth_port_active[direction] != -1;
  //dont need to explicitly check for static port. 
  //If there is atleast one active port, there will be a static port available.

  if (route_available) {
    uint32_t route_eth_id = static_route ? my_node_info->eth_port_static[direction] : my_node_info->eth_port_active[direction];
    tag = get_sys_addr_tag(my_node_info->my_chip_x, my_node_info->my_chip_y, eth_id_get_x(route_eth_id), eth_id_get_y(route_eth_id));
  }
  return tag;
}

eth_conn_info_t get_nearest_rack_x_direction (uint32_t my_rack_y, eth_conn_info_t conn_id) {

  uint32_t min_distance = 0xFFFF; // initialize to large number.
  eth_conn_info_t direction = eth_conn_info_t::ETH_UNCONNECTED;
  for (uint32_t e = 0; e < MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2; e++) {
    if ((sys_connections->rack_to_rack_router_chips[e] & 0xF) == conn_id) {
      uint32_t rack_y = e >> 3;
      bool above_me = rack_y > my_rack_y;
      uint32_t distance =  above_me ? rack_y - my_rack_y : my_rack_y - rack_y;
      if (distance < min_distance) {
        min_distance = distance;
        direction = above_me ? ETH_RACK_U : ETH_RACK_D;
      }
    }
  }
  return direction;
}

void get_sorted_eth_ids_for_connection(uint32_t min, uint32_t max, eth_conn_info_t conn, uint8_t *conn_array)
{
  for (uint32_t i = 0; i < NUM_ETH_INST; i++) {
    if (i >= min && i <= max && eth_conn_info[i] == conn) {
      conn_array[i] = i;
    } else {
      conn_array[i] = 0xFF;
    }
  }

  for (uint32_t i = 0; i < NUM_ETH_INST - 1; i++) {
    for (uint32_t j = i+1; j < NUM_ETH_INST; j++) {
      if (conn_array[j] < conn_array[i]) {
        uint32_t temp = conn_array[i];
        conn_array[i] = conn_array[j];
        conn_array[j] = temp;
      }
    }
  }
}

int8_t get_eth_port(uint32_t my_eth_id, int32_t min_eth_id, int32_t max_eth_id, eth_conn_info_t conn)
{
//  int8_t eth_id = (int8_t) min_eth_id;
//  if (min_eth_id != -1) {
//    uint32_t range = (max_eth_id - min_eth_id) & 0x3;
//    uint32_t port = min_eth_id + (my_eth_id & range);
//    if (eth_conn_info[port] == eth_conn_info[min_eth_id]) {
//      eth_id = (int8_t) port;
//    }
//  }
//  return eth_id;

  uint32_t my_conn = eth_conn_info[my_eth_id];
  bool shift_links = false;

  if (config_buf->board_type == 0) {
    //galaxy board.
    bool i_am_x = my_conn == eth_conn_info_t::ETH_CONN_L || my_conn == eth_conn_info_t::ETH_CONN_R || my_conn == eth_conn_info_t::ETH_RACK_U || my_conn == eth_conn_info_t::ETH_RACK_D;
    shift_links = i_am_x && (conn == eth_conn_info_t::ETH_CONN_U || conn == eth_conn_info_t::ETH_CONN_D || conn == eth_conn_info_t::ETH_RACK_L || conn == eth_conn_info_t::ETH_RACK_R);
  }

  int8_t eth_id = (int8_t) min_eth_id;
  uint8_t sorted_eth_ids[NUM_ETH_INST];
  if (min_eth_id != -1) {
    get_sorted_eth_ids_for_connection(min_eth_id, max_eth_id, conn, sorted_eth_ids);
    
    bool outgoing_port = ((int32_t)my_eth_id >= min_eth_id) && ((int32_t)my_eth_id <= max_eth_id);
    if (outgoing_port && eth_conn_info[my_eth_id] == conn) {
      //I am a connected port in correct direction.
      eth_id = (int8_t)my_eth_id;
    } else {
      //I am either unconnected or connected in different direction.
      //Need to find a port in required direction.
      uint32_t index = my_eth_id & 0x3;
      if (shift_links) {
        index += (my_node_info->my_chip_y & 0x3);
        index &= 0x3;
      }

      if (eth_conn_info[sorted_eth_ids[index]] == conn) {
        eth_id = (int8_t)sorted_eth_ids[index];
      } else {
        index = my_eth_id & 0x1;
        if (eth_conn_info[sorted_eth_ids[index]] == conn) {
          eth_id = (int8_t)sorted_eth_ids[index];
        } else {
          eth_id = (int8_t)sorted_eth_ids[0];
        }
      }
    }
  }
  return eth_id;
}

uint32_t get_local_eth_id(eth_conn_info_t conn, uint32_t remote_eth_id)
{
  uint32_t eth_id = 0;
  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    if ((eth_conn_info[e] == conn) && (remote_eth_ids[e] == remote_eth_id)) {
      eth_id = e;
      break;
    }
  }
  return eth_id;
}

uint32_t read_eth_id_from_noc(uint32_t eth_id, uint32_t dest_rdptr_offset) {
  uint32_t alignment_offset = (dest_rdptr_offset >> 2) & 0x7; //32-byte alignment
  noc_read(NOC_XY_ADDR(eth_id_get_x(eth_id), eth_id_get_y(eth_id), dest_rdptr_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 4);
  uint32_t remote_eth_id = noc_scratch_buf[alignment_offset];
  return remote_eth_id;
}

void get_remote_eth_ids()
{
  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    if (eth_conn_info[e] > ETH_UNCONNECTED) {
      remote_eth_ids[e] = read_eth_id_from_noc(e, (uint32_t)&remote_chip_id_dest);
    } else {
      remote_eth_ids[e] = 0xFF;
    }
  }
}

uint32_t get_remote_board_type(eth_conn_info_t conn)
{
  uint32_t data = 0;
  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    if (eth_conn_info[e] == conn) {
      data = read_eth_id_from_noc(e, (uint32_t)&remote_board_type);
      break;
    }
  }
  return data;
}

void get_sorted_remote_eth_ids_for_connection(eth_conn_info_t conn, uint8_t *conn_array)
{
  for (uint32_t i = 0; i < NUM_ETH_INST; i++) {
    if (eth_conn_info[i] == conn) {
      conn_array[i] = remote_eth_ids[i];
    } else {
      conn_array[i] = 0xFF;
    }
  }

  for (uint32_t i = 0; i < NUM_ETH_INST - 1; i++) {
    for (uint32_t j = i+1; j < NUM_ETH_INST; j++) {
      if (conn_array[j] < conn_array[i]) {
        uint32_t temp = conn_array[i];
        conn_array[i] = conn_array[j];
        conn_array[j] = temp;
      }
    }
  }
}

void get_sorted_eth_ids_for_connection(eth_conn_info_t conn, uint8_t *conn_array)
{
  for (uint32_t i = 0; i < NUM_ETH_INST; i++) {
    if (eth_conn_info[i] == conn) {
      conn_array[i] = i;
    } else {
      conn_array[i] = 0xFF;
    }
  }

  for (uint32_t i = 0; i < NUM_ETH_INST - 1; i++) {
    for (uint32_t j = i+1; j < NUM_ETH_INST; j++) {
      if (conn_array[j] < conn_array[i]) {
        uint32_t temp = conn_array[i];
        conn_array[i] = conn_array[j];
        conn_array[j] = temp;
      }
    }
  }
}

void get_directional_connections(eth_route_direction_t direction, int32_t &min, int32_t &max)
{
  uint32_t link_count = links_per_dir[direction];
  if (link_count > 0 && direction >= eth_route_direction_t::RACK_LEFT && direction <= eth_route_direction_t::RACK_DOWN) {
    uint32_t board_type = get_remote_board_type((eth_conn_info_t)(direction + eth_conn_info_t::ETH_CONN_L));
    if (config_buf->board_type != board_type) {
      if (config_buf->board_type > 0 && board_type == 0) {
        //Nebula connected to Galaxy.
        //Use all available ports to route traffice from NB to Galaxy.
        my_node_info->nebula_to_galxy = true;
      } else if (config_buf->board_type == 0) {
        //Galaxy board connected to Nebula
        //Dont route traffic from Galaxy to Nebula.
        link_count = 0;
      }
    }
  }

  if (link_count == 0) {
    min = -1;
    max = -1;
  } else {
    uint32_t links = link_count;
    int32_t local_min = -1;
    int32_t local_max = 0;
    for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
      if (links == 0) {
        break;
      }
      if (eth_conn_info[e] == direction + ETH_CONN_L) {
        if (local_min == -1) {
          local_min = e;
        }
        local_max = e;
        links--;
      }
    }
    min = local_min;
    max = local_max;
  }
}

void __attribute__((noinline)) set_noc_q_sizes() {

  for (uint32_t i = 0; i < eth_route_direction_t::COUNT; i++) {
    links_per_dir[i] = 0;
  }

  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {
    if (eth_conn_info[e] > ETH_UNCONNECTED) {
      links_per_dir[eth_conn_info[e] - ETH_CONN_L]++;
    }
    /*
    if (eth_conn_info[e] == ETH_CONN_L) {
      links_per_dir[eth_route_direction_t::LEFT]++;
    } else if (eth_conn_info[e] == ETH_CONN_R) {
      links_per_dir[eth_route_direction_t::RIGHT]++;
    } else if (eth_conn_info[e] == ETH_CONN_U) {
      links_per_dir[eth_route_direction_t::UP]++;
    } else if (eth_conn_info[e] == ETH_CONN_D) {
      links_per_dir[eth_route_direction_t::DOWN]++;
    } else if (eth_conn_info[e] == ETH_RACK_R) {
      links_per_dir[eth_route_direction_t::RACK_RIGHT]++;
    } else if (eth_conn_info[e] == ETH_RACK_L) {
      links_per_dir[eth_route_direction_t::RACK_LEFT]++;
    } else if (eth_conn_info[e] == ETH_RACK_U) {
      links_per_dir[eth_route_direction_t::RACK_UP]++;
    } else if (eth_conn_info[e] == ETH_RACK_D) {
      links_per_dir[eth_route_direction_t::RACK_DOWN]++;
    }
    */
  }

  for (uint32_t e = 0; e < NUM_ETH_INST; e++) {

    //if there is only one ethernet port connected in a direction,
    //we need to halve the outstanding requests issued on that port to avoid
    //deadlocks when traffic is flowing in both directions over the link.
    //The single ethernet port is full duplex reflected by default q size of NODE_CMD_BUF_SIZE/2
    uint8_t q_size = NODE_CMD_BUF_SIZE;
    uint32_t full_duplex = 0;

    if (eth_conn_info[e] > ETH_UNCONNECTED) {
      if (links_per_dir[eth_conn_info[e] - ETH_CONN_L] <= 1) {
        //there is only 1 port in a direction.
        //Port is full duplex.
        q_size = NODE_CMD_BUF_SIZE/2;
        full_duplex = 1 << e;
      }
    }
/*    
    if (eth_conn_info[e] == ETH_CONN_L) {
      if (links_per_dir[eth_route_direction_t::LEFT] & 0xFE) {
        //there are more than 1 ports in a direction. We will use separate ethernet
        //ports to achieve full duplex. Each port is half duplex.
        q_size = NODE_CMD_BUF_SIZE;
        full_duplex = 0;
      }
    } else if (eth_conn_info[e] == ETH_CONN_R) {
      if (links_per_dir[eth_route_direction_t::RIGHT] & 0xFE) {
        q_size = NODE_CMD_BUF_SIZE;
        full_duplex = 0;
      }
    } else if (eth_conn_info[e] == ETH_CONN_U) {
      if (links_per_dir[eth_route_direction_t::UP] & 0xFE) {
        q_size = NODE_CMD_BUF_SIZE;
        full_duplex = 0;
      }
    } else if (eth_conn_info[e] == ETH_CONN_D) {
      if (links_per_dir[eth_route_direction_t::DOWN] & 0xFE) {
        q_size = NODE_CMD_BUF_SIZE;
        full_duplex = 0;
      }
    } else if (eth_conn_info[e] == ETH_RACK_R || eth_conn_info[e] == ETH_RACK_L) {
      if (links_per_dir[eth_route_direction_t::RACK_X] & 0xFE) {
        q_size = NODE_CMD_BUF_SIZE;
        full_duplex = 0;
      }
    } else if (eth_conn_info[e] == ETH_RACK_U || eth_conn_info[e] == ETH_RACK_D) {
      if (links_per_dir[eth_route_direction_t::RACK_Y] & 0xFE) {
        q_size = NODE_CMD_BUF_SIZE;
        full_duplex = 0;
      }
    }
    */
    my_node_info->eth_port_q_size[e] = q_size;
    my_node_info->eth_port_full_duplex |= full_duplex;
  }

}

uint32_t __attribute__((noinline)) init_rack_y_route(eth_conn_info_t conn)
{
  uint32_t min_distance = 256;
  uint32_t distance;
  uint32_t my_chip_y = my_node_info->my_chip_y;
  int32_t router_index = -1;
  for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
    if ((sys_connections->shelf_router_chips[e] & 0xF) == conn) {
      uint32_t y = get_chip_y_from_router_index(e);
      if (y > my_chip_y) {
        distance = y - my_chip_y;
      } else {
        distance = my_chip_y - y;
      }
      if (distance < min_distance) {
        min_distance = distance;
        router_index = e;
      }
    }
  }
  uint32_t tag;
  if (router_index != -1) {
    int32_t eth_id = (sys_connections->shelf_router_chips[router_index] >> 4) & 0xF;
    if (router_index == get_y_router_chip_index(my_node_info->my_chip_x, my_node_info->my_chip_y)) {
      eth_id = my_node_info->eth_port_active[conn - ETH_CONN_L];
    }
    if (eth_id == -1) {
      tag = -1;
    } else {
      tag = get_sys_addr_tag(get_chip_x_from_router_index(router_index), get_chip_y_from_router_index(router_index), eth_id_get_x(eth_id), eth_id_get_y(eth_id));
    }
  } else {
    tag = -1;
  }
  return tag;
}

uint32_t init_rack_x_route(eth_conn_info_t conn)
{
  uint32_t rack_x_unconnected = 1;
  uint32_t min_distance = 256;
  uint32_t distance;
  uint32_t my_rack_y = my_node_info->my_rack_y;
  uint32_t my_chip_x = my_node_info->my_chip_x;
  uint32_t tag = -1;

  //Determine whether this shelf has a connection to next rack on left or right side.
  //Initialize as not connected, then clear to 0 if atleast one connection is found
  //for right or left side rack.
  for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
    if ((sys_connections->shelf_router_chips[e] & 0xF) == conn) {
      rack_x_unconnected = 0;
    }
  }
  if (rack_x_unconnected) {
    //This shelf has no connection to next rack in specified direction.
    //Need to go up/down a shelf on same rack until we get to a shelf that provides 
    //a connection to next rack.
    eth_conn_info_t rack_x_detour_direction = get_nearest_rack_x_direction(my_rack_y, conn);
    if (rack_x_detour_direction == eth_conn_info_t::ETH_RACK_U) {
      tag = my_node_info->rack_u_sys_addr_tag;
    } else if (rack_x_detour_direction == eth_conn_info_t::ETH_RACK_D) {
      tag = my_node_info->rack_d_sys_addr_tag;
    }
  } else {
    int32_t router_index = 0;

    for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
      if ((sys_connections->shelf_router_chips[e] & 0xF) == conn) {
        uint32_t x = get_chip_x_from_router_index(e);
        if (x > my_chip_x) {
          distance = x - my_chip_x;
        } else {
          distance = my_chip_x - x;
        }
        if (distance < min_distance) {
          min_distance = distance;
          router_index = e;
        }
      }
    }

    uint32_t eth_id = (sys_connections->shelf_router_chips[router_index] >> 4) & 0xF;
    if (router_index == get_x_router_chip_index(my_node_info->my_chip_x, my_node_info->my_chip_y)) {
      eth_id = my_node_info->eth_port_active[conn - ETH_CONN_L];
    }
    tag = get_sys_addr_tag(get_chip_x_from_router_index(router_index), get_chip_y_from_router_index(router_index), eth_id_get_x(eth_id), eth_id_get_y(eth_id));

  }
  return tag;
}

uint32_t get_broadcast_tag_from_noc(uint32_t eth_id) {
  uint32_t dest_rdptr_offset = (uint32_t)&remote_chip_id_dest;
  uint32_t alignment_offset = (dest_rdptr_offset >> 2) & 0x7; //32-byte alignment
  noc_read(NOC_XY_ADDR(eth_id_get_x(eth_id), eth_id_get_y(eth_id), dest_rdptr_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 16);
  uint32_t remote_rack_x = noc_scratch_buf[alignment_offset + 2] & CHIP_COORD_MASK;
  uint32_t remote_rack_y = (noc_scratch_buf[alignment_offset + 2] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
  uint32_t remote_chip_x = noc_scratch_buf[alignment_offset + 1] & CHIP_COORD_MASK;
  uint32_t remote_chip_y = (noc_scratch_buf[alignment_offset + 1] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
  uint32_t broadcast_tag = get_broadcast_tag(remote_rack_x, remote_rack_y, remote_chip_x, remote_chip_y);
  return broadcast_tag;
}

void __attribute__((noinline)) __attribute__((section(".erisc_exec_once"))) init_routing_connections() {

  uint32_t my_eth_id = my_node_info->my_eth_id;
  uint32_t my_chip_x = my_node_info->my_chip_x;
  uint32_t my_chip_y = my_node_info->my_chip_y;
  uint32_t my_rack_x = my_node_info->my_rack_x;
  uint32_t my_rack_y = my_node_info->my_rack_y;

  set_noc_q_sizes();
  get_remote_eth_ids();

  for (int d = eth_route_direction_t::LEFT; d <= eth_route_direction_t::RACK_DOWN; d++) {
    int32_t min_eth_id;
    int32_t max_eth_id;
    int8_t port;

    get_directional_connections((eth_route_direction_t)d, min_eth_id, max_eth_id);
    my_node_info->eth_port_min[d] = (int8_t)min_eth_id;
    my_node_info->eth_port_max[d] = (int8_t)max_eth_id;
    port = get_eth_port(my_eth_id, min_eth_id, max_eth_id, (eth_conn_info_t)(d + eth_conn_info_t::ETH_CONN_L));
    my_node_info->eth_port_active[d] = port;
    my_node_info->eth_port_static[d] = port;
  }

  /*
  get_directional_connections(eth_route_direction_t::UP, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::UP] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::UP] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::UP] = port;
  my_node_info->eth_port_static[eth_route_direction_t::UP] = port;

  get_directional_connections(eth_route_direction_t::RIGHT, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::RIGHT] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::RIGHT] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::RIGHT] = port;
  my_node_info->eth_port_static[eth_route_direction_t::RIGHT] = port;

  get_directional_connections(eth_route_direction_t::DOWN, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::DOWN] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::DOWN] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::DOWN] = port;
  my_node_info->eth_port_static[eth_route_direction_t::DOWN] = port;

  get_directional_connections(eth_route_direction_t::LEFT, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::LEFT] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::LEFT] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::LEFT] = port;
  my_node_info->eth_port_static[eth_route_direction_t::LEFT] = port;

  get_directional_connections(eth_route_direction_t::RACK_UP, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::RACK_UP] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::RACK_UP] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::RACK_UP] = port;
  my_node_info->eth_port_static[eth_route_direction_t::RACK_UP] = port;

  get_directional_connections(eth_route_direction_t::RACK_RIGHT, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::RACK_RIGHT] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::RACK_RIGHT] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::RACK_RIGHT] = port;
  my_node_info->eth_port_static[eth_route_direction_t::RACK_RIGHT] = port;

  get_directional_connections(eth_route_direction_t::RACK_DOWN, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::RACK_DOWN] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::RACK_DOWN] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::RACK_DOWN] = port;
  my_node_info->eth_port_static[eth_route_direction_t::RACK_DOWN] = port;

  get_directional_connections(eth_route_direction_t::RACK_LEFT, min_eth_id, max_eth_id);
  my_node_info->eth_port_min[eth_route_direction_t::RACK_LEFT] = (int8_t)min_eth_id;
  my_node_info->eth_port_max[eth_route_direction_t::RACK_LEFT] = (int8_t)max_eth_id;
  port = get_eth_port(my_eth_id, min_eth_id, max_eth_id);
  my_node_info->eth_port_active[eth_route_direction_t::RACK_LEFT] = port;
  my_node_info->eth_port_static[eth_route_direction_t::RACK_LEFT] = port;
*/

  my_node_info->broadcast_tags[eth_route_direction_t::UP] = -1;
  my_node_info->broadcast_tags[eth_route_direction_t::RIGHT] = -1;
  my_node_info->broadcast_tags[eth_route_direction_t::DOWN] = -1;
  my_node_info->broadcast_tags[eth_route_direction_t::LEFT] = -1;

  if (my_node_info->eth_port_active[eth_route_direction_t::UP] != -1) {
    my_node_info->broadcast_tags[eth_route_direction_t::UP] = get_broadcast_tag(my_rack_x, my_rack_y, my_chip_x, my_chip_y+1);
  }
  if (my_node_info->eth_port_active[eth_route_direction_t::RIGHT] != -1) {
    my_node_info->broadcast_tags[eth_route_direction_t::RIGHT] = get_broadcast_tag(my_rack_x, my_rack_y, my_chip_x+1, my_chip_y);
  }
  if (my_node_info->eth_port_active[eth_route_direction_t::DOWN] != -1) {
    my_node_info->broadcast_tags[eth_route_direction_t::DOWN] = get_broadcast_tag(my_rack_x, my_rack_y, my_chip_x, my_chip_y-1);
  }
  if (my_node_info->eth_port_active[eth_route_direction_t::LEFT] != -1) {
    my_node_info->broadcast_tags[eth_route_direction_t::LEFT] = get_broadcast_tag(my_rack_x, my_rack_y, my_chip_x-1, my_chip_y);
  }

  // TODO: add error conditions for when the above links can't be found

  uint32_t remote_eth_chip_x = remote_chip_id_dest[1] & CHIP_COORD_MASK;
  uint32_t remote_eth_chip_y = (remote_chip_id_dest[1] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK;
  uint32_t remote_eth_id = remote_chip_id_dest[0];
  my_node_info->remote_eth_sys_addr = eth_conn_info[my_eth_id] > ETH_UNCONNECTED ? get_sys_addr(remote_eth_chip_x, remote_eth_chip_y, eth_id_get_x(remote_eth_id), eth_id_get_y(remote_eth_id), 0) : -1;
  my_node_info->remote_eth_rack = eth_conn_info[my_eth_id] > ETH_UNCONNECTED ? remote_chip_id_dest[2] : -1;
  my_node_info->my_sys_addr_tag = get_sys_addr_tag(my_chip_x, my_chip_y, eth_id_get_x(my_eth_id), eth_id_get_y(my_eth_id));

/*
  my_node_info->rack_route_x_sys_addr = min_eth_id_rack_x == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_rack_x),
                                                    eth_id_get_y(min_eth_id_rack_x));
  my_node_info->rack_route_y_sys_addr = min_eth_id_rack_y == -1 ? -1 : get_sys_addr_tag(my_chip_x,
                                                    my_chip_y,
                                                    eth_id_get_x(min_eth_id_rack_y),
                                                    eth_id_get_y(min_eth_id_rack_y));
*/
  my_node_info->rack_u_sys_addr_tag = init_rack_y_route(eth_conn_info_t::ETH_RACK_U);
  my_node_info->rack_d_sys_addr_tag = init_rack_y_route(eth_conn_info_t::ETH_RACK_D);
  //init_rack_x_route must always be called after rack up/down route has been determined.
  //That is because if the shelf has no rack x connection, We want to detour to a 
  //shelf up/down. For that rack up/down sys addr tag must have been calculated prior to
  //calling init_rack_x_route.
  my_node_info->rack_l_sys_addr_tag = init_rack_x_route(eth_conn_info_t::ETH_RACK_L);
  my_node_info->rack_r_sys_addr_tag = init_rack_x_route(eth_conn_info_t::ETH_RACK_R);

  int min_router_id_rack_u = -1;
  int min_router_id_rack_r = -1;
  int min_router_id_rack_d = -1;
  int min_router_id_rack_l = -1;

  /*
  Broadcast transactions hop from shelf to shelf in Rack X or Rack Y direction using the
  lowest numbered router chip connected in respective direction.
  Determine the lowest routiner chip and see if I am an ethernet core on that Chip.
  Then set the broadcast tag accordingly.
  */
  for (uint32_t i = 0; i < MAX_EDGE_ROUTERS; i++) {
    if ((min_router_id_rack_u == -1) && ((sys_connections->shelf_router_chips[i] & 0xF) == ETH_RACK_U)) {
      min_router_id_rack_u = i;
    }
    if ((min_router_id_rack_d == -1) && ((sys_connections->shelf_router_chips[i] & 0xF) == ETH_RACK_D)) {
      min_router_id_rack_d = i;
    }
  }

  for (uint32_t i = 0; i < (MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2); i++) {
    if ((min_router_id_rack_l == -1) && ((sys_connections->rack_to_rack_router_chips[i] & 0xF) == ETH_RACK_L)) {
      min_router_id_rack_l = i;
    }
    if ((min_router_id_rack_r == -1) && ((sys_connections->rack_to_rack_router_chips[i] & 0xF) == ETH_RACK_R)) {
      min_router_id_rack_r = i;
    }
  }

  int32_t router_chip_index = get_y_router_chip_index(my_chip_x, my_chip_y);
  if (router_chip_index != -1) {
    //I am a chip capable of Rack Y routing
    if ((min_router_id_rack_u == router_chip_index) || (min_router_id_rack_d == router_chip_index)) {
      //I am an ethernet core on lowest rack y router chip.
      uint8_t conn_type = sys_connections->shelf_router_chips[router_chip_index] & 0xF;
      uint32_t eth_id = (sys_connections->shelf_router_chips[router_chip_index] >> 4) & 0xF;
      // Check whether the shelf to shelf connection has been disabled in case its a 
      // Galaxy to Nebula route.
      uint32_t route_dir = conn_type - ETH_CONN_L;
      if (my_node_info->eth_port_active[route_dir] != -1) {
        if (conn_type == eth_conn_info[my_eth_id]) {
          //I am an ethernet core connected to another shelf in Y direction.
          uint32_t broadcast_tag = get_broadcast_tag(remote_chip_id_dest[2] & CHIP_COORD_MASK, (remote_chip_id_dest[2] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK, remote_eth_chip_x, remote_eth_chip_y);
          if (my_chip_x == 0) {
            my_node_info->broadcast_tags[eth_route_direction_t::LEFT] = broadcast_tag | BROADCAST_RACK_XY_LINK;
            my_node_info->eth_broadcast_skip[eth_route_direction_t::LEFT] = 1;
          } else if (my_chip_x == 3) {
            my_node_info->broadcast_tags[eth_route_direction_t::RIGHT] = broadcast_tag | BROADCAST_RACK_XY_LINK;
            my_node_info->eth_broadcast_skip[eth_route_direction_t::RIGHT] = 1;
          }
        } else {
          uint32_t broadcast_tag = get_broadcast_tag_from_noc(eth_id);
          if (my_chip_x == 0) {
            my_node_info->broadcast_tags[eth_route_direction_t::LEFT] = broadcast_tag | BROADCAST_RACK_XY_LINK;
          } else if (my_chip_x == 3) {
            my_node_info->broadcast_tags[eth_route_direction_t::RIGHT] = broadcast_tag | BROADCAST_RACK_XY_LINK;
          }
        }
      } else {
        //This particular Shelf Y connection is going from Galaxy board to Nebula board.
        //Disable Broadcast on such links.
        if (my_chip_x == 0) {
          my_node_info->broadcast_tags[eth_route_direction_t::LEFT] = -1;
          my_node_info->eth_broadcast_skip[eth_route_direction_t::LEFT] = 1;
        } else if (my_chip_x == 3) {
          my_node_info->broadcast_tags[eth_route_direction_t::RIGHT] = -1;
          my_node_info->eth_broadcast_skip[eth_route_direction_t::RIGHT] = 1;
        }
      }
    }
  }

  router_chip_index = get_x_router_chip_index(my_chip_x, my_chip_y);
  if (router_chip_index != -1) {
    //I am a chip capable of Rack X routing
    int32_t rack_router_chip_index = my_rack_y * MAX_BOARD_CONNECTIONS_X * 2;
    rack_router_chip_index += shelf_router_to_rack_router_chip_index(router_chip_index);
    if ((min_router_id_rack_l == rack_router_chip_index) || (min_router_id_rack_r == rack_router_chip_index)) {
      //I am an ethernet core on lowest rack x router chip.
      uint8_t conn_type = sys_connections->rack_to_rack_router_chips[rack_router_chip_index] & 0xF;
      uint32_t eth_id = (sys_connections->rack_to_rack_router_chips[rack_router_chip_index] >> 4) & 0xF;
      // Check whether the shelf to shelf connection has been disabled in case its a 
      // Galaxy to Nebula route.
      uint32_t route_dir = conn_type - ETH_CONN_L;
      if (my_node_info->eth_port_active[route_dir] != -1) {
        if (conn_type == eth_conn_info[my_eth_id]) {
          uint32_t broadcast_tag = get_broadcast_tag(remote_chip_id_dest[2] & CHIP_COORD_MASK, (remote_chip_id_dest[2] >> CHIP_COORD_Y_SHIFT) & CHIP_COORD_MASK, remote_eth_chip_x, remote_eth_chip_y);
          if (my_chip_y == 0) {
            my_node_info->broadcast_tags[eth_route_direction_t::DOWN] = broadcast_tag | BROADCAST_RACK_XY_LINK;
            my_node_info->eth_broadcast_skip[eth_route_direction_t::DOWN] = 1;
          } else if (my_chip_y == 7) {
            my_node_info->broadcast_tags[eth_route_direction_t::UP] = broadcast_tag | BROADCAST_RACK_XY_LINK;
            my_node_info->eth_broadcast_skip[eth_route_direction_t::UP] = 1;
          }
        } else {
          uint32_t broadcast_tag = get_broadcast_tag_from_noc(eth_id);
          if (my_chip_y == 0) {
            my_node_info->broadcast_tags[eth_route_direction_t::DOWN] = broadcast_tag | BROADCAST_RACK_XY_LINK;
          } else if (my_chip_y == 7) {
            my_node_info->broadcast_tags[eth_route_direction_t::UP] = broadcast_tag | BROADCAST_RACK_XY_LINK;
          }
        }
      } else {
        //This particular Shelf X connection is going from Galaxy board to Nebula board.
        //Disable Broadcast on such links.
        if (my_chip_y == 0) {
          my_node_info->broadcast_tags[eth_route_direction_t::DOWN] = -1;
          my_node_info->eth_broadcast_skip[eth_route_direction_t::DOWN] = 1;
        } else if (my_chip_y == 7) {
          my_node_info->broadcast_tags[eth_route_direction_t::UP] = -1;
          my_node_info->eth_broadcast_skip[eth_route_direction_t::UP] = 1;
        }
      }
    }
  }
  uint32_t my_conn = eth_conn_info[my_eth_id];
  if (my_conn == ETH_CONN_L) {
    my_node_info->eth_broadcast_skip[eth_route_direction_t::LEFT] = 1;
    my_node_info->eth_broadcast_skip[eth_route_direction_t::UP] = 1;
    my_node_info->eth_broadcast_skip[eth_route_direction_t::DOWN] = 1;
  } else if (my_conn == ETH_CONN_R) {
    my_node_info->eth_broadcast_skip[eth_route_direction_t::RIGHT] = 1;
    my_node_info->eth_broadcast_skip[eth_route_direction_t::UP] = 1;
    my_node_info->eth_broadcast_skip[eth_route_direction_t::DOWN] = 1;
  } else if (my_conn == ETH_CONN_U) {
    my_node_info->eth_broadcast_skip[eth_route_direction_t::UP] = 1;
  } else if (my_conn == ETH_CONN_D) {
    my_node_info->eth_broadcast_skip[eth_route_direction_t::DOWN] = 1;
  }

  my_node_info->delimiter = 0xabcd1234;

}

//void __attribute__((noinline)) __attribute__((section(".misc_init"))) copy_to_all_eth (uint32_t addr)
void __attribute__((noinline)) copy_to_all_eth (uint32_t addr)

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

uint32_t shelf_router_to_rack_router_chip_index(uint32_t i)
{
  uint32_t index = 0;
  if (i > 19) {
    index = i - 16;
  } else if (i > 7) {
    index = i - 8;
  }
  return index;
}

bool is_rack_router_x(uint32_t i)
{
  bool res = false;
  if ((i > 19 && i < 24) || (i > 7 && i < 12)) {
    res = true;
  }
  return res;
}

//void __attribute__((noinline)) __attribute__((section(".misc_init"))) populate_missing_routers_low(uint32_t my_chip_x, uint32_t my_chip_y, eth_conn_info_t direction)
void __attribute__((noinline)) __attribute__((section(".erisc_exec_once"))) populate_missing_routers_low(uint32_t my_chip_x, uint32_t my_chip_y, eth_conn_info_t direction)

{
  if (direction == eth_conn_info_t::ETH_CONN_L) {
    for (int y = 0; y < MAX_BOARD_CONNECTIONS_Y; y++) {
      if (y < (int)my_chip_y || my_chip_x != 0) {
        int32_t router_chip_index = get_y_router_chip_index(0, y);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_R) {
    for (int y = MAX_BOARD_CONNECTIONS_Y - 1; y >=0; y--) {
      if (y > (int)my_chip_y || my_chip_x != MAX_BOARD_CONNECTIONS_X-1) {
        int32_t router_chip_index = get_y_router_chip_index(MAX_BOARD_CONNECTIONS_X-1, y);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_U) {
    for (int x = 0; x < MAX_BOARD_CONNECTIONS_X; x++) {
      if (x < (int)my_chip_x || my_chip_y != MAX_BOARD_CONNECTIONS_Y-1) {
        int32_t router_chip_index = get_x_router_chip_index(x, MAX_BOARD_CONNECTIONS_Y-1);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_D) {
    for (int x = MAX_BOARD_CONNECTIONS_X - 1; x >= 0; x--) {
      if (x > (int)my_chip_x || my_chip_y != 0) {
        int32_t router_chip_index = get_x_router_chip_index(x, 0);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  }
}

void __attribute__((noinline)) __attribute__((section(".erisc_exec_once"))) populate_missing_routers_high(uint32_t my_chip_x, uint32_t my_chip_y, eth_conn_info_t direction)
{

  if (direction == eth_conn_info_t::ETH_CONN_L) {
    if (my_chip_x == 0) {
      for (int y = my_chip_y + 1; y < MAX_BOARD_CONNECTIONS_Y; y++) {
        int32_t router_chip_index = get_y_router_chip_index(my_chip_x, y);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_R) {
    if (my_chip_x == MAX_BOARD_CONNECTIONS_X - 1) {
      for (int y = my_chip_y - 1; y >= 0; y--) {
        int32_t router_chip_index = get_y_router_chip_index(my_chip_x, y);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_U) {
    if (my_chip_y == MAX_BOARD_CONNECTIONS_Y - 1) {
      for (int x = my_chip_x + 1; x < MAX_BOARD_CONNECTIONS_X; x++) {
        int32_t router_chip_index = get_x_router_chip_index(x, my_chip_y);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  } else if (direction == eth_conn_info_t::ETH_CONN_D) {
    if (my_chip_y == 0) {
      for (int x = my_chip_x - 1; x >= 0; x--) {
        int32_t router_chip_index = get_x_router_chip_index(x, my_chip_y);
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
        copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
      }
    }
  }
}

void populate_missing_shelves_high(uint32_t rack_y)
{
  //this function is called by top shelf to set all shelves above top shelf to unconnected.
  for (int y = rack_y + 1; y < MAX_RACK_SHELVES; y++) {
    for (int i = 0; i < MAX_BOARD_CONNECTIONS_X * 2; i++) {
      //set all connections above the top shelf to unconnected.
      uint32_t index = y * MAX_BOARD_CONNECTIONS_X * 2 + i;
      rack_to_rack_router_chips[index] = (0xF << 4) | ETH_UNCONNECTED;
      copy_to_all_eth((uint32_t)&rack_to_rack_router_chips[index]);
    }
  }
}

void __attribute__((noinline)) __attribute__((section(".erisc_exec_once")))my_node_info_init() {

  // zero out all queue/pointer structures:
  CQ_MAILBOX = ETH_ROUTING_STRUCT_ADDR;
  volatile uint32_t* ptr = ((volatile uint32_t*)ETH_ROUTING_STRUCT_ADDR);
  for (uint32_t i = 0; i < ETH_QUEUES_SIZE_BYTES/4; i++) {
    ptr[i] = 0;
  }

  //initialize shelf discovery temporary buffer to 0.
  for (uint32_t i = 0; i < (MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2); i++) {
    rack_router_chips[i] = 0;
  }
  //initialize rack discovery temporary buffer to 0.
  for (uint32_t i = 0; i < (MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2); i++) {
    rack_to_rack_router_chips[i] = 0;
  }

  volatile uint32_t *p_sys_conn = (volatile uint32_t *)ETH_CONN_INFO_ADDR;
  for (uint32_t i = 0; i < sizeof(system_connection_t)/4; i++) {
    p_sys_conn[i] = 0;
  }

  for (uint32_t i = 0; i < 4; i++) {
    discovery_req_out[i] = 0;
    discovery_req_in[i] = 0;
    discovery_resp_out[i] = 0;
    discovery_resp_in[i] = 0;
  }

  // first 5 are set to 0 by the test script
  for (uint32_t i = 5; i < RESULTS_BUF_SIZE/4; i++) {
    test_results[i] = 0;
  }

  // clear node info
  volatile uint32_t* node_info = (volatile uint32_t*)(NODE_INFO_ADDR);

  for (uint32_t i = 0; i < (NODE_INFO_SIZE/4); i++) {
    node_info[i] = 0;
  }

  // clear config buffer space
  for (uint32_t i = 0; i < (DEBUG_BUF_SIZE)/4; i++) {
    debug_buf[i] = 0;
  }

  mac_rcv_fail = 0;

  refclk_counter_init();
  init_timestamps();

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

  eth_conn_info[my_eth_id] = conn_info;
  my_node_info->routing_node = conn_info > ETH_UNCONNECTED;

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
        rack_router_chips[router_chip_index] = ((int16_t)min_eth_id_u << 4) | ETH_RACK_U;
      } else if (min_eth_id_d != -1) {
        rack_router_chips[router_chip_index] = ((int16_t)min_eth_id_d << 4) | ETH_RACK_D;
      } else {
        rack_router_chips[router_chip_index] = (0xf << 4) | ETH_UNCONNECTED;
      }
      copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
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
        rack_router_chips[router_chip_index] = ((uint16_t)min_eth_id_l << 4) | ETH_RACK_L;
      } else if (min_eth_id_r != -1) {
        rack_router_chips[router_chip_index] = ((uint16_t)min_eth_id_r << 4) | ETH_RACK_R;
      } else {
        rack_router_chips[router_chip_index] = (0xF << 4) | ETH_UNCONNECTED;
      }
      copy_to_all_eth((uint32_t)&rack_router_chips[router_chip_index]);
    }
  }

  bool left_edge = false;
  bool right_edge = false;
  bool top_edge = false;
  bool bot_edge = false;

  //propagate Y router chips.
  //if I am the minimum eth connected in U or D direction,
  int min_eth_id_u = -1;
  int min_eth_id_r = -1;
  int min_eth_id_d = -1;
  int min_eth_id_l = -1;
  int min_eth_id_rack_u = -1;
  int min_eth_id_rack_d = -1;

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
    if ((min_eth_id_rack_u == -1) && (eth_conn_info[e] == ETH_RACK_U)) {
      min_eth_id_rack_u = e;
    }
    if ((min_eth_id_rack_d == -1) && (eth_conn_info[e] == ETH_RACK_D)) {
      min_eth_id_rack_d = e;
    }
  }
  left_edge = min_eth_id_l == -1;
  right_edge = min_eth_id_r == -1;
  top_edge = min_eth_id_u == -1;
  bot_edge = min_eth_id_d == -1;

  if (my_eth_id == 0) {
    if (bot_edge && left_edge) {
      populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_L);
      populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_D);
    }
    if (top_edge && left_edge) {
      populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_L);
      populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_U);
    }
    if (top_edge && right_edge) {
      populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_U);
      populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_R);
    }
    if (bot_edge && right_edge) {
      populate_missing_routers_high(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_R);
      populate_missing_routers_low(my_chip_x, my_chip_y, eth_conn_info_t::ETH_CONN_D);
    }
  }


  //Wait for connection info to arrive from all the router chips.
  bool shelf_discovery_worker = my_eth_id == (uint32_t)min_eth_id_d || my_eth_id == (uint32_t)min_eth_id_l || my_eth_id == (uint32_t)min_eth_id_r || my_eth_id == (uint32_t)min_eth_id_u;

  if (shelf_discovery_worker) {
    while (1) {
      //send conn data on eth link.
      //check no pending req before sending new data.
      if (discovery_req_out[3] == discovery_resp_in[3]) {
        for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
          if ((conn_info_sent & (0x1 << e)) == 0) {
            if (rack_router_chips[e] != ETH_UNKNOWN) {
              discovery_req_out[0] = e;
              discovery_req_out[1] = rack_router_chips[e];
              discovery_req_out[3]++;
              conn_info_sent |= (0x1 << e);
              eth_send_packet(0, ((uint32_t)discovery_req_out)>>4, ((uint32_t)discovery_req_in)>>4, 1);
              //break for loop after sending 1 request.
              //need to wait for resp before sending more.
              break;
            }
          }
        }
      }

      //got some data on ethernet link
      if (discovery_req_in[3] != discovery_resp_out[3]) {
        uint32_t router_index = discovery_req_in[0];
        if ((conn_info_rcvd & (0x1 << router_index)) == 0) {
          if (rack_router_chips[router_index] == ETH_UNKNOWN) {
            rack_router_chips[router_index] = discovery_req_in[1];
            copy_to_all_eth((uint32_t)&rack_router_chips[router_index]);
          }
          conn_info_rcvd |= (0x1 << router_index);
        }
        discovery_resp_out[3] = discovery_req_in[3];
        eth_send_packet(0, ((uint32_t)discovery_resp_out)>>4, ((uint32_t)discovery_resp_in)>>4, 1);
      }
      eth_link_status_check();
      if (conn_info_sent == 0x00FFFFFF && conn_info_rcvd == 0x00FFFFFF) {
        break;
      }
    }
  }

  //Wait for connection info to arrive from all the router chips.
  for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
    while (rack_router_chips[e] == ETH_UNKNOWN) {eth_link_status_check();}
  }

  //At this point shelf connectivity table is complete.
  //sys_connections->rack_router_chips contains info on which chips are connected in RACK-X and RACK-Y direction.
  //Next every shelf has to send its RACK-X connection info to other shelves in the Rack.

  bool top_shelf = true;
  for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
    //if shelf has atleast 1 Rack Up connection, then its not the top shelf.
    if ((rack_router_chips[e] & 0xF) == ETH_RACK_U) {
      top_shelf = false;
    }
  }

  // From the completed shelf connectivity table, pick the chips that are providing rack-to-rack (i.e RACK-X) connections.
  // Use the Ethernet core 0 on bottom left corner chip of the shelf to do this work.
  if (bot_edge && left_edge) {
    if (my_eth_id == 0) {
      if (top_shelf) {
        //if this shelf also happens to be the top shelf, Populate any missing shelves with unconnected link status.
        populate_missing_shelves_high(my_rack_y);
      }

      for (uint32_t e = 0; e < MAX_EDGE_ROUTERS; e++) {
        if (is_rack_router_x(e)) {
          uint32_t router_chip_index = my_rack_y * MAX_BOARD_CONNECTIONS_X * 2;
          router_chip_index += shelf_router_to_rack_router_chip_index(e);
          rack_to_rack_router_chips[router_chip_index] = rack_router_chips[e];
          copy_to_all_eth((uint32_t)&rack_to_rack_router_chips[router_chip_index]);
        }
      }
    }
  }

  bool rack_discovery_worker = my_eth_id == (uint32_t)min_eth_id_rack_d || my_eth_id == (uint32_t)min_eth_id_rack_u;
  //In cases where I am not a worker, but I am connected to a worker ethernet port on the other chip, send abort
  //to neighboring chip. Routing info will propagate to neighboring chip through the rack_discovery_worker
  //ethernet port on my chip.
  //This message is dont-care if my neighobring chip is a non discovery worker as well.
  bool send_discovery_abort = !rack_discovery_worker && (conn_info == ETH_RACK_U || conn_info == ETH_RACK_D);


  if (shelf_discovery_worker || rack_discovery_worker) {
    while (1) {
      //send conn data on eth link.
      //check no pending req before sending new data.
      if (discovery_req_out[3] == discovery_resp_in[3]) {
        for (uint32_t e = 0; e < MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2; e++) {
          uint32_t word_index = e >> 5;
          uint32_t bit_index = e & 0x1F;
          if ((rack_conn_info_sent[word_index] & (0x1 << bit_index)) == 0) {
            if (rack_to_rack_router_chips[e] != ETH_UNKNOWN) {
              discovery_req_out[0] = e;
              discovery_req_out[1] = rack_to_rack_router_chips[e];
              discovery_req_out[3]++;
              rack_conn_info_sent[word_index] |= (0x1 << bit_index);
              eth_send_packet(0, ((uint32_t)discovery_req_out)>>4, ((uint32_t)discovery_req_in)>>4, 1);
              //break for loop after sending 1 request.
              //need to wait for resp before sending more.
              break;
            }
          }
        }
      }

      //got some data on ethernet link
      if (discovery_req_in[3] != discovery_resp_out[3]) {
        uint32_t e = discovery_req_in[0];
        //Received code to abort rack discovery.
        if (e == 0xDEAD) {
          break;
        }
        uint32_t word_index = e >> 5;
        uint32_t bit_index = e & 0x1F;
        if ((rack_conn_info_rcvd[word_index] & (0x1 << bit_index)) == 0) {
          if (rack_to_rack_router_chips[e] == ETH_UNKNOWN) {
            rack_to_rack_router_chips[e] = discovery_req_in[1];
            copy_to_all_eth((uint32_t)&rack_to_rack_router_chips[e]);
          }
          rack_conn_info_rcvd[word_index] |= (0x1 << bit_index);
        }
        discovery_resp_out[3] = discovery_req_in[3];
        eth_send_packet(0, ((uint32_t)discovery_resp_out)>>4, ((uint32_t)discovery_resp_in)>>4, 1);
      }

      eth_link_status_check();
      if (rack_conn_info_sent[0] == 0xFFFFFFFF && rack_conn_info_sent[1] == 0xFFFFFFFF && rack_conn_info_sent[2] == 0x0000FFFF
       && rack_conn_info_rcvd[0] == 0xFFFFFFFF && rack_conn_info_rcvd[1] == 0xFFFFFFFF && rack_conn_info_rcvd[2] == 0x0000FFFF) {
        break;
      }
    }
  } else if (send_discovery_abort) {
    discovery_req_out[0] = 0xDEAD;
    discovery_req_out[1] = 0xDEAD;
    discovery_req_out[3]++;
    eth_send_packet(0, ((uint32_t)discovery_req_out)>>4, ((uint32_t)discovery_req_in)>>4, 1);
  }

  //Wait for connection info to arrive from all the router chips.
  for (uint32_t e = 0; e < MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2; e++) {
    while (rack_to_rack_router_chips[e] == ETH_UNKNOWN) {eth_link_status_check();}
  }

  //copy over all shelf/rack routing info from temp buffers to sys_connections
  //initialize shelf discovery temporary buffer to 0.
  for (uint32_t i = 0; i < (MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2); i++) {
    sys_connections->shelf_router_chips[i] = rack_router_chips[i];
  }
  //initialize rack discovery temporary buffer to 0.
  for (uint32_t i = 0; i < (MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2); i++) {
    sys_connections->rack_to_rack_router_chips[i] = rack_to_rack_router_chips[i];
  }

  init_routing_connections();
}

void eth_routing_init() {
  eth_queues = ((eth_queues_t *)ETH_ROUTING_STRUCT_ADDR);
  my_node_info_init();
}
#define ETH_PORT_SWITCH_THR 50
void inc_eth_txn_counter(eth_route_direction_t direction) {
  if (direction >= eth_route_direction_t::LEFT && direction <= eth_route_direction_t::DOWN) {
    my_node_info->eth_port_txn_count[direction]++;
    if (my_node_info->eth_port_txn_count[direction] >= ETH_PORT_SWITCH_THR) {
      int32_t port = my_node_info->eth_port_active[direction];
      port++;
      while (1) {
        if (port > my_node_info->eth_port_max[direction]) {
          port = my_node_info->eth_port_min[direction];
        }
        if (eth_conn_info[port] > eth_conn_info_t::ETH_UNCONNECTED) {
          my_node_info->eth_port_active[direction] = port;
          my_node_info->eth_port_txn_count[direction] = 0;
          break;
        } else {
          port++;
        }
      }
    }
  }
}

void get_next_hop_sys_addr(uint32_t static_route, uint32_t rack, uint64_t dest_addr, uint32_t &next_hop_rack, uint64_t &next_hop_sys_addr, eth_route_direction_t &direction)
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
  uint32_t rack_route_tag;
  bool rack_hop_route = false;

  if (!sys_addr_is_local_rack(rack, my_node_info)) {
    //get the chip x/y of the chip that connects to next shelf/rack
    if (dest_rack_x < my_rack_x) {
      //dest rack is to the left of my rack.
      rack_route_tag = my_node_info->rack_l_sys_addr_tag;
      //FIXME It may be rack up/down if this shelf has no rack x connections.
      direction = eth_route_direction_t::RACK_LEFT;
    } else if (dest_rack_x > my_rack_x) {
      rack_route_tag = my_node_info->rack_r_sys_addr_tag;
      direction = eth_route_direction_t::RACK_RIGHT;
    } else if (dest_rack_y < my_rack_y) {
      rack_route_tag = my_node_info->rack_d_sys_addr_tag;
      direction = eth_route_direction_t::RACK_DOWN;
    } else if (dest_rack_y > my_rack_y) {
      rack_route_tag = my_node_info->rack_u_sys_addr_tag;
      direction = eth_route_direction_t::RACK_UP;
    }
    
    dest_chip_x = sys_addr_tag_get_chip_x(rack_route_tag);
    dest_chip_y = sys_addr_tag_get_chip_y(rack_route_tag);
    dest_eth_id = sys_addr_tag_get_erisc_dest_index(rack_route_tag);
    //pick my_eth_id incase its a host request, and the host erisc is 
    //connected in the same direction as dest_eth_id.
    //Otherwise make a potential noc hop to dest_eth_id.

    if ((int32_t)rack_route_tag == -1) {
      //we do not have any link on current shelf to get to next rack/shelf.
      next_hop_sys_addr = -1;
      return;
    } else {
      if (dest_chip_x == my_chip_x && dest_chip_y == my_chip_y) {
        //we are at the chip that has a link to next rack/shelf
        rack_hop_route = true;
        //If my ethernet link is the same connection type as dest_eth_id,
        //skip the noc hop to dest_eth_id. Make the ethernet hop from my ethernet
        //link.
        //Cannot do this as the node may be for incoming traffic.
        //if (eth_conn_info[dest_eth_id] == eth_conn_info[my_node_info->my_eth_id]) {
        //  dest_eth_id = my_node_info->my_eth_id;
        //}
      }
    }
  }
  bool next_hop_x = (dest_chip_x != my_chip_x);
  bool next_hop_eth_link = false;

  uint32_t dest_eth_link_chip_x = sys_addr_get_chip_x(my_node_info->remote_eth_sys_addr);
  uint32_t dest_eth_link_chip_y = sys_addr_get_chip_y(my_node_info->remote_eth_sys_addr);

  //next_hop_eth_link should be set only if this ethernet link is to a chip on the same shelf.
  //otherwise, this ethernet link is to a different rack/shelf and shoudl be resoved by rack_routing.
  if (sys_addr_is_local_rack(my_node_info->remote_eth_rack, my_node_info)) {
    if (next_hop_x) {
      if (dest_chip_x > my_chip_x) {
        next_hop_eth_link = dest_eth_link_chip_x > my_chip_x;
        direction = eth_route_direction_t::RIGHT;
      } else if (dest_chip_x < my_chip_x) {
        next_hop_eth_link = dest_eth_link_chip_x < my_chip_x;
        direction = eth_route_direction_t::LEFT;
      }
    } else {
      if (dest_chip_y > my_chip_y) {
        next_hop_eth_link = dest_eth_link_chip_y > my_chip_y;
        direction = eth_route_direction_t::UP;
      } else if (dest_chip_y < my_chip_y) {
        next_hop_eth_link = dest_eth_link_chip_y < my_chip_y;
        direction = eth_route_direction_t::DOWN;
      }
    }
    if (next_hop_eth_link) {
      //qualifay next hop eth link with whether this link is allowed outgoing transactions.
      bool outgoing_link = (my_node_info->my_eth_id >= my_node_info->eth_port_min[direction]) && (my_node_info->my_eth_id <= my_node_info->eth_port_max[direction]);
      next_hop_eth_link &= outgoing_link;
    }

  } else if (rack_hop_route) {
    //we need to to go a different rack/shelf and
    //we are at the chip that has a link to next rack/shelf
    //then check if this ethernet core is supposed to be making the ethernet hop.
    if (my_node_info->my_eth_id == dest_eth_id) {
      //we are at an ethernet node that is connected to next rack/shelf
      next_hop_eth_link = true;
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
      next_hop_sys_addr_local = rack_route_tag;
    } else {
      if (next_hop_x) {
        if (dest_chip_x > my_chip_x) {
          next_hop_sys_addr_local = get_shelf_route_tag(static_route, eth_route_direction_t::RIGHT);
          direction = eth_route_direction_t::RIGHT;
        } else {
          next_hop_sys_addr_local = get_shelf_route_tag(static_route, eth_route_direction_t::LEFT);
          direction = eth_route_direction_t::LEFT;
        }
      } else {
        if (dest_chip_y > my_chip_y) {
          next_hop_sys_addr_local = get_shelf_route_tag(static_route, eth_route_direction_t::UP);
          direction = eth_route_direction_t::UP;
        } else {
          next_hop_sys_addr_local = get_shelf_route_tag(static_route, eth_route_direction_t::DOWN);
          direction = eth_route_direction_t::DOWN;
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

void cmd_service_error(node_cmd_t* req_cmd, node_cmd_t* resp_cmd) {
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

void cmd_service_local(node_cmd_t* req_cmd, node_cmd_t* resp_cmd) {
  uint32_t* req_addr = (uint32_t*)(sys_addr_get_offset(req_cmd->sys_addr));

  if (req_cmd->flags & CMD_DATA_BLOCK) {
    //block mode, use noc write to complete this request.
    cmd_service_noc(req_cmd, resp_cmd);
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

void cmd_service_broadcast_noc(uint32_t q_id, node_cmd_t* req_cmd, node_cmd_t* resp_cmd) {

  uint64_t dest_offset = sys_addr_get_offset(req_cmd->sys_addr);
  uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;
  uint32_t alignment_offset = (dest_offset >> 2) & 0x7; //32-byte alignment

  uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
  int32_t buf_id = req_cmd->src_resp_buf_index;;
  uint32_t data_buf = buf_id * MAX_BLOCK_SIZE;
  data_buf += q_id ? ETH_IN_DATA_BUFFER_ADDR : ETH_OUT_DATA_BUFFER_ADDR;
  num_bytes -= BROADCAST_HEADER_SIZE_BYTES;

  volatile broadcast_header_t *bcast_header = (volatile broadcast_header_t *)data_buf;
  data_buf += BROADCAST_HEADER_SIZE_BYTES;

  bool skip_shelf = (bcast_header->shelf_exclude_mask[my_node_info->my_rack_x] >> my_node_info->my_rack_y) & 0x1;
  bool skip_chip = ((bcast_header->chip_exclude_mask >> (my_node_info->my_chip_x * MAX_BOARD_CONNECTIONS_Y + my_node_info->my_chip_y)) & 0x1);
  bool no_flush = (req_cmd->flags & LAST_ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE;

  if (skip_shelf == 0 && skip_chip == 0) {
    if (num_bytes == 4) {
      //If broadcast start address is not 32-byte aligned and data is > 4 bytes, broadcast will be broken by runtime into
      //multiple 4-byte writes till we hit a 32-byte boundary. After which we will receive > 4 byte sized broadcast blocks
      //that will be 32-byte aligned.
      //Otherwise, num_bytes can be 4 if broadcast data is only 4 bytes to begin with.
      noc_scratch_buf[alignment_offset] = ((volatile uint32_t *)data_buf)[0];
      data_buf = (uint32_t)noc_scratch_buf + alignment_offset * 4;
    }
    //Read back row harvesting mask since we don't want to write to harvested rows.
    //Clear ethernet row exclusion bits. [0] and [6]
    //Eth exclusion is set in ROUTER_CFG_3 not because of harvesting but because NOC hw broadcast is 
    //set to only include Tensix Cores. So all ethernet rows and harvested rows are set to be excluded in ROUTER_CFG_3.
    //Whether to exclude Eth rows or not will be determeined by bcast_header->row_exclude_mask.
    uint32_t harvest_mask = NOC_CFG_READ_REG(noc_id, ROUTER_CFG_3);

    if (req_cmd->flags & CMD_USE_NOC_BROADCAST) {
      while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
      // Clear ethernet rows as they cannot be harvested.
      if (noc_id == 0) {
        harvest_mask &= 0xFBE;
      } else {
        harvest_mask &= 0x7DF;
      }
      uint32_t mcast_acks = 80; // max acks when none of the Tensix rows are harvested.
      for (int i = 0; i < 12; i++) {
        if ((0x1 << i) & harvest_mask) {
          mcast_acks -= 8; // for each harvested row, 8 Tensix acks will not come.
        }
      }

      uint32_t write_acks = NOC_STATUS_READ_REG(noc_id, NIU_MST_WR_ACK_RECEIVED);
      ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                            data_buf,
                            NOC_MULTICAST_ADDR(0, 0, 9, 11, dest_offset),
                            num_bytes,
                            4, true, false, 1, NCRISC_WR_DEF_TRID);
      write_acks += mcast_acks; //Number of expected acks for this mcast.

      while (NOC_STATUS_READ_REG(noc_id, NIU_MST_WR_ACK_RECEIVED) != write_acks);
      ncrisc_noc_clear_outstanding_reqs(noc_id, NCRISC_WR_DEF_TRID);
    } else {
      // Clear ethernet rows as they cannot be harvested.
      if (noc_id == 0) {
        harvest_mask &= 0xFFFFFFBE;
      } else {
        harvest_mask &= 0xFFFFF7DF;
      }
      uint32_t row_exclude_mask = harvest_mask | bcast_header->row_exclude_mask;
      for (uint32_t dest_noc_y = 0; dest_noc_y < 12; dest_noc_y++) {
        if (row_exclude_mask & (0x1 << dest_noc_y)) {
          //skip this row
          continue;
        }
        for (uint32_t dest_noc_x = 0; dest_noc_x < 10; dest_noc_x++) {
          if (bcast_header->col_exclude_mask & (0x1 << dest_noc_x)) {
            //skip this column
            continue;
          }
          while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
          ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                                data_buf,
                                NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset),
                                num_bytes,
                                ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        }
      }
      if (no_flush == false) {
        while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
        //wait_write_flush(noc_id, NCRISC_WR_DEF_TRID, 0xded10000);
      }
    }
  }
  if (no_flush) {
    resp_cmd->flags = ORDERED_DRAM_BLOCK_WRITE_ACK;
  } else {
    resp_cmd->flags = CMD_BROADCAST | CMD_WR_ACK | CMD_DATA_BLOCK | (req_cmd->flags & (CMD_DATA_BLOCK_DRAM | CMD_LAST_DATA_BLOCK_DRAM | CMD_USE_NOC_BROADCAST));
  }
}

void noc_write (uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t size, bool no_flush)
{
  while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
  ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                        src_addr,
                        dest_addr,
                        size,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  if (no_flush == false) {
    while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
    //wait_write_flush(noc_id, NCRISC_WR_DEF_TRID, 0xded20000);
  }
}

void noc_write_unaligned(uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t size) {

  uint32_t total_words_to_write = size >> 2;
  for (uint32_t w = 0; w < total_words_to_write; ) {
    uint32_t align_index = (dest_addr >> 2) & 0x7;
    uint32_t words_to_noc_write = 8 - align_index;
    if (w + words_to_noc_write > total_words_to_write) {
      //for the last write trim the words from 8 to the requied number
      //of writes for the tail end.
      words_to_noc_write = total_words_to_write - w;
    }
    for (uint32_t word = 0; word < words_to_noc_write; word++) {
      noc_scratch_buf[word + align_index] = ((uint32_t *)src_addr)[word];
    }
    while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF)){}
    ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                        ((uint32_t)noc_scratch_buf) + 4*align_index,
                        dest_addr,
                        words_to_noc_write << 2,
                        noc_id, false, false, 1, NCRISC_WR_DEF_TRID);
    src_addr += words_to_noc_write << 2;
    dest_addr += words_to_noc_write << 2;
    w += words_to_noc_write;
  }
  while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
}

void noc_read(uint64_t src_addr, uint32_t dest_addr, uint32_t size)
{
  while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
  ncrisc_noc_fast_read(0, NCRISC_SMALL_TXN_CMD_BUF, src_addr, dest_addr, size, NCRISC_RD_DEF_TRID);
  while(!ncrisc_noc_reads_flushed(0, NCRISC_RD_DEF_TRID));
}

uint32_t scatter_write(node_cmd_t* req_cmd, node_cmd_t* resp_cmd)
{
  uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;
  uint32_t result = 0;
  bool from_noc_queue = req_cmd != resp_cmd;

  int32_t buf_id = req_cmd->src_resp_buf_index;;
  uint32_t sp_start = from_noc_queue ? ETH_OUT_DATA_BUFFER_ADDR : ETH_DATA_BUFFER_ADDR;
  sp_start += buf_id * MAX_BLOCK_SIZE;
  uint32_t sp_offset = 0;

  while (sp_offset < MAX_BLOCK_SIZE) {
    uint32_t * p_ss_start = (uint32_t *) (sp_start + sp_offset);

    uint32_t scatr_cmd = p_ss_start[0] & 0xF;
    uint32_t scatr_flags = (p_ss_start[0] >> 4) & 0xF;
    uint32_t scatr_count = (p_ss_start[0] >> 8) & 0xFF;
    uint32_t p_size = p_ss_start[1] & 0xFF;
    uint32_t p_offset = (p_ss_start[1] >> 8) & 0xFF;
    bool payload_per_offset = (scatr_flags & 0x1) != 0;
    int64_t start_addr = (p_ss_start[0] >> 16 );
    start_addr <<= 32;
    start_addr |= p_ss_start[2];

    if (scatr_cmd != 1) {
      //not a valid scatter section.
      //Treat as end of scatter page.
      if (scatr_cmd != 0xF) {
        result = CMD_ERROR;
      }
      break;
    }
    if (p_offset == 0 || p_size == 0) {
      result = CMD_ERROR;
      break;
    }

    int32_t *p_scatr_offset = (int32_t *) (sp_start + sp_offset + 8);

    for (uint32_t i = 0; i < scatr_count; i++) {
      uint32_t src_alignment_offset = (((uint32_t)p_ss_start >> 2) + p_offset) & 0x7;
      int32_t scatr_offset = *p_scatr_offset;
      p_scatr_offset++;
      int64_t dest_offset = start_addr;
      if (i > 0) {
        dest_offset += (int64_t)scatr_offset;
      }
      uint32_t dest_alignment_offset = (dest_offset >> 2) & 0x7; //32-byte alignment

      if (src_alignment_offset == dest_alignment_offset) {
        //both source and destination data addresses have the same alignment.
        //we don't need to use the scratch buf to realign the addresses.
        noc_write(noc_id, (uint32_t)p_ss_start + p_offset * 4, dest_offset, p_size * 4, false);
      } else {
        //non-aligned addresses. Need to use scratch buffer to align src address with destination address.
        noc_write_unaligned(noc_id, (uint32_t)p_ss_start + p_offset * 4, dest_offset, p_size * 4);
      }
      if (payload_per_offset) {
        p_offset += p_size;
      }
    }
    if (!payload_per_offset) {
        p_offset += p_size;
    }
    sp_offset = sp_offset + p_offset * 4;
  }
  return result;
}

void cmd_service_noc(node_cmd_t* req_cmd, node_cmd_t* resp_cmd) {

  uint32_t dest_noc_x = sys_addr_get_noc_x(req_cmd->sys_addr);
  uint32_t dest_noc_y = sys_addr_get_noc_y(req_cmd->sys_addr);
  uint64_t dest_offset = sys_addr_get_offset(req_cmd->sys_addr);
  uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;
  uint32_t alignment_offset = (dest_offset >> 2) & 0x7; //32-byte alignment
  bool from_noc_queue = req_cmd != resp_cmd;

  if (req_cmd->flags & CMD_DATA_BLOCK) {

    uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
    int32_t buf_id = req_cmd->src_resp_buf_index;;
    uint32_t data_buf = from_noc_queue ? ETH_OUT_DATA_BUFFER_ADDR : ETH_DATA_BUFFER_ADDR;
    uint32_t flags_local = req_cmd->flags;
    data_buf += buf_id * MAX_BLOCK_SIZE;

    if (req_cmd->flags & CMD_WR_REQ) {
      /*
      while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
      ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                          data_buf,
                          NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset),
                          num_bytes,
                          ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
      while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
      */
      bool no_flush = (flags_local & LAST_ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE;
      if (flags_local & CMD_MOD) {
        uint32_t result = scatter_write(req_cmd, resp_cmd);
        resp_cmd->flags = result | CMD_WR_ACK | CMD_DATA_BLOCK | CMD_MOD | (flags_local & (CMD_DATA_BLOCK_DRAM | CMD_LAST_DATA_BLOCK_DRAM));
      } else {
        noc_write(noc_id, data_buf, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset), num_bytes, no_flush);
        resp_cmd->flags = CMD_WR_ACK | CMD_DATA_BLOCK | (flags_local & (CMD_DATA_BLOCK_DRAM | CMD_LAST_DATA_BLOCK_DRAM));
      }
      if (no_flush) {
        resp_cmd->flags = ORDERED_DRAM_BLOCK_WRITE_ACK;
      }
    } else {
      while (!ncrisc_noc_fast_read_ok(noc_id, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_read(noc_id, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset), data_buf, num_bytes, NCRISC_RD_DEF_TRID);
      while(!ncrisc_noc_reads_flushed(noc_id, NCRISC_RD_DEF_TRID));
      resp_cmd->data = req_cmd->data; // return the number of bytes being sent back to requestor.
      resp_cmd->flags = CMD_RD_DATA | CMD_DATA_BLOCK | (flags_local & (CMD_DATA_BLOCK_DRAM | CMD_LAST_DATA_BLOCK_DRAM));
    }

  } else {
    if (req_cmd->flags & CMD_WR_REQ) {
      noc_scratch_buf[alignment_offset] = req_cmd->data;
      /*
      while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
      ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                          (uint32_t)noc_scratch_buf + alignment_offset * 4,
                          NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset),
                          4,
                          ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
      while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
      */
      noc_write(noc_id, (uint32_t)noc_scratch_buf + alignment_offset * 4, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset), 4, false);
      resp_cmd->flags = CMD_WR_ACK;
    } else {
      while (!ncrisc_noc_fast_read_ok(noc_id, NCRISC_SMALL_TXN_CMD_BUF));
      ncrisc_noc_fast_read(noc_id, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 4, NCRISC_RD_DEF_TRID);
      while(!ncrisc_noc_reads_flushed(noc_id, NCRISC_RD_DEF_TRID));
      resp_cmd->flags = CMD_RD_DATA;
      resp_cmd->data = noc_scratch_buf[alignment_offset];
      noc_scratch_buf[7] = resp_cmd->data;
    }
  }
}

bool req_fwd_noc(node_cmd_t* req_cmd, uint64_t next_hop_sys_addr) {

  uint32_t dest_noc_x = sys_addr_get_noc_x(next_hop_sys_addr);
  uint32_t dest_noc_y = sys_addr_get_noc_y(next_hop_sys_addr);
  bool fwd_success = false;

  //Read NOC Response Q of noc hop.
  //Need to make sure Response Q of noc hop is not full before we can
  //forward the command.
  uint32_t dest_rdptr_offset = (uint32_t)&eth_queues->node_resp_cmd_q[eth_directional_q_id_t::ETH_OUT].wrptr.ptr;
  uint32_t alignment_offset = (dest_rdptr_offset >> 2) & 0x7; //32-byte alignment
  /*
  while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
  ncrisc_noc_fast_read(0, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_rdptr_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 4, NCRISC_RD_DEF_TRID);
  while(!ncrisc_noc_reads_flushed(0, NCRISC_RD_DEF_TRID));
  */
  noc_read(NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_rdptr_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 32);

  //uint32_t dest_resp_wrptr = noc_scratch_buf[0];
  uint32_t dest_resp_rdptr = noc_scratch_buf[4];

  if (my_node_info->req_pending[eth_q_id_t::NOC] == 0) {
    uint64_t read_addr = NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], (uint32_t)&my_node_info->wr_ptr[eth_q_id_t::NOC]);
    uint64_t wr_ptr_addr = NOC_XY_ADDR(dest_noc_x, dest_noc_y, (uint32_t)&eth_queues->node_req_cmd_q[eth_directional_q_id_t::ETH_OUT].wrptr.ptr);
    while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
    noc_atomic_read_and_increment(0, NCRISC_SMALL_TXN_CMD_BUF, wr_ptr_addr, 1, 3, read_addr, false, NCRISC_WR_DEF_TRID);
  }

  dest_rdptr_offset = (uint32_t)&eth_queues->node_req_cmd_q[eth_directional_q_id_t::ETH_OUT].rdptr.ptr;
  alignment_offset = (dest_rdptr_offset >> 2) & 0x7; //32-byte alignment
/*
  while (!ncrisc_noc_fast_read_ok(0, NCRISC_SMALL_TXN_CMD_BUF));
  ncrisc_noc_fast_read(0, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_rdptr_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 4, NCRISC_RD_DEF_TRID);
  while(!ncrisc_noc_reads_flushed(0, NCRISC_RD_DEF_TRID));
*/
  noc_read(NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_rdptr_offset), (uint32_t)noc_scratch_buf + alignment_offset * 4, 4);
  uint32_t dest_rdptr = noc_scratch_buf[alignment_offset];
  uint32_t dest_wrptr = my_node_info->wr_ptr[eth_q_id_t::NOC];

  uint32_t dest_wr_index = dest_wrptr & NODE_CMD_BUF_SIZE_MASK;

  uint32_t dest_addr = (uint32_t)(&(eth_queues->node_req_cmd_q[eth_directional_q_id_t::ETH_OUT].cmd[dest_wr_index]));
  int32_t buf_id = req_cmd->src_resp_buf_index;
  if (!node_cmd_q_ptrs_full(dest_wrptr, dest_rdptr) && !node_cmd_q_ptrs_full(dest_wrptr, dest_resp_rdptr)) {

    req_cmd->src_resp_q_id = eth_q_id_t::NOC;
    req_cmd->src_noc_x = my_node_info->my_noc_x[0];
    req_cmd->src_noc_y = my_node_info->my_noc_y[0];
    req_cmd->Valid = 1;

    if ((req_cmd->flags & (CMD_DATA_BLOCK | CMD_WR_REQ)) == (CMD_DATA_BLOCK | CMD_WR_REQ))
    {
      uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
      uint32_t src_buf_addr = ETH_IN_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
      uint32_t dest_buf_addr = ETH_OUT_DATA_BUFFER_ADDR + dest_wr_index * MAX_BLOCK_SIZE;
      while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
      ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                          src_buf_addr,
                          NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_buf_addr),
                          num_bytes,
                          ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);

    }

    //*fwd_cmd_ptr = *req_cmd;
    copy_eth_cmd((uint32_t)req_cmd, (uint32_t)noc_scratch_buf, sizeof(node_cmd_t)/4);

/*
    while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                          (uint32_t)noc_scratch_buf,
                          NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_addr),
                          NODE_CMD_SIZE_BYTES,
                          ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
    while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
*/
    noc_write(ROUTING_NOC, (uint32_t)noc_scratch_buf, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_addr), NODE_CMD_SIZE_BYTES, false);
    my_node_info->req_pending[eth_q_id_t::NOC] = 0;
    fwd_success = true;

  } else {
    my_node_info->req_pending[eth_q_id_t::NOC] = 1;
  }
  return fwd_success;
}

void resp_fwd_host(uint32_t q_id, node_cmd_t* node_resp_cmd) {

  cmd_q_t* resp_cmd_q = &eth_queues->resp_cmd_q;
  cmd_q_t* req_cmd_q = &eth_queues->req_cmd_q[eth_q_id_t::HOST];
  uint32_t resp_cmd_q_index = node_resp_cmd->src_resp_buf_index;
  routing_cmd_t* resp_cmd = resp_cmd_q->cmd + resp_cmd_q_index;
  bool write_resp = true;

  uint32_t resp_flags = node_resp_cmd->flags;
  uint32_t resp_data = node_resp_cmd->data;

  if (resp_flags & CMD_DEST_UNREACHABLE) {
    req_cmd_q->cmd_counters.error++;
  }

  if (!node_cmd_error(node_resp_cmd) && ((node_resp_cmd->flags & (CMD_DATA_BLOCK | CMD_RD_DATA)) == (CMD_DATA_BLOCK | CMD_RD_DATA)))
  {
    uint32_t num_bytes = node_resp_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
    uint32_t local_buf_id = node_resp_cmd->local_resp_buf_index;
    uint32_t src_buf_addr = local_buf_id * MAX_BLOCK_SIZE;
    src_buf_addr += q_id ? ETH_IN_DATA_BUFFER_ADDR : ETH_OUT_DATA_BUFFER_ADDR;
    uint64_t dest_buf_addr;

    if (resp_flags & CMD_DATA_BLOCK_DRAM) {
      uint32_t dram_bytes_processed = my_node_info->dram_bytes_processed_resp;
      dest_buf_addr = resp_cmd->src_addr_tag + dram_bytes_processed;

      if (resp_flags & CMD_LAST_DATA_BLOCK_DRAM) {
        my_node_info->dram_bytes_processed_resp = 0;
        resp_data = dram_bytes_processed + num_bytes;
        resp_flags &= ~CMD_LAST_DATA_BLOCK_DRAM;
        //finished writing all remote read data to dram.
      } else {
        my_node_info->dram_bytes_processed_resp += num_bytes;
        //not done with writing all dram data.
        //should not write response to host yet.
        write_resp = false;
      }
      /*
      while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
      ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        src_buf_addr,
                        NOC_XY_ADDR(0, 3, (0x800000000 | dest_buf_addr)),
                        num_bytes,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
      */
      noc_write(ROUTING_NOC, src_buf_addr, NOC_XY_ADDR(0, 3, (0x800000000 | dest_buf_addr)), num_bytes, false);
    } else {
      dest_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + resp_cmd_q_index * MAX_BLOCK_SIZE;
      /*
      while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
      ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        src_buf_addr,
                        NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], dest_buf_addr),
                        num_bytes,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
      */
      noc_write(ROUTING_NOC, src_buf_addr, NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], dest_buf_addr), num_bytes, false);
    }
    //while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
  }

#ifdef ENABLE_TIMESTAMPING
  if (node_resp_cmd->timestamp) {
    uint32_t index = node_resp_cmd->timestamp & 0x7F;
    uint64_t now = eth_read_wall_clock();
    uint64_t diff = now - routing_cmd_timestamp[index];
    eth_queues->cmd_latency[index] = diff;
  }
#endif

  if (resp_flags & CMD_WR_ACK) {
    req_cmd_q->cmd_counters.wr_resp++;
  } else if (resp_flags & CMD_RD_DATA) {
    req_cmd_q->cmd_counters.rd_resp++;
    if (write_resp) {
      resp_cmd->data = resp_data;
      resp_cmd->flags = resp_flags;
    }
  }
}

void resp_fwd_noc(node_cmd_t* resp_cmd) {

  //node_cmd_t* fwd_cmd_ptr = (node_cmd_t*)noc_scratch_buf;

  uint32_t dest_wr_index = resp_cmd->src_resp_buf_index;
  uint32_t dest_noc_x = resp_cmd->src_noc_x;
  uint32_t dest_noc_y = resp_cmd->src_noc_y;
  uint32_t dest_addr = (uint32_t)(&(eth_queues->node_resp_cmd_q[eth_directional_q_id_t::ETH_IN].cmd[dest_wr_index]));
  int32_t remote_buf_id = resp_cmd->src_resp_buf_index;
  int32_t local_buf_id = resp_cmd->local_resp_buf_index;


  if (!node_cmd_error(resp_cmd) && ((resp_cmd->flags & (CMD_DATA_BLOCK | CMD_RD_DATA)) == (CMD_DATA_BLOCK | CMD_RD_DATA)))
  {
    uint32_t num_bytes = resp_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
    uint32_t src_buf_addr = ETH_OUT_DATA_BUFFER_ADDR + local_buf_id * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_IN_DATA_BUFFER_ADDR + remote_buf_id * MAX_BLOCK_SIZE;
    while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        src_buf_addr,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_buf_addr),
                        num_bytes,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  }

  //*fwd_cmd_ptr = *resp_cmd;
  copy_eth_cmd((uint32_t)resp_cmd, (uint32_t)noc_scratch_buf, sizeof(node_cmd_t)/4);
/*
  while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF)){}
    ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        (uint32_t)noc_scratch_buf,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_addr),
                        NODE_CMD_SIZE_BYTES/2,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
*/
  noc_write(ROUTING_NOC, (uint32_t)noc_scratch_buf, NOC_XY_ADDR(dest_noc_x, dest_noc_y, dest_addr), NODE_CMD_SIZE_BYTES/2, false);
}

void req_fwd_eth(routing_cmd_t* req_cmd) {

  uint32_t eth_wr_index = eth_queues->eth_out_req_cmd_q.wrptr.ptr & CMD_BUF_SIZE_MASK;
  uint32_t buf_id = req_cmd->src_resp_buf_index;

  if ((req_cmd->flags & (CMD_DATA_BLOCK | CMD_WR_REQ)) == (CMD_DATA_BLOCK | CMD_WR_REQ))
  {
    uint32_t num_words = ((req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4))) + 0xF ) >> 4;
    uint32_t src_buf_addr = ETH_OUT_DATA_BUFFER_ADDR + buf_id * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_DATA_BUFFER_ADDR + eth_wr_index * MAX_BLOCK_SIZE;
    eth_send_packet(0, src_buf_addr>>4, dest_buf_addr>>4, num_words);
  }

  req_cmd->src_resp_q_id = eth_q_id_t::ETH;
  //eth_queues->eth_out_req_cmd_q.cmd[eth_wr_index] = *req_cmd;
  copy_eth_cmd((uint32_t)req_cmd, (uint32_t)(&eth_queues->eth_out_req_cmd_q.cmd[eth_wr_index]), sizeof(routing_cmd_t)/4);
  cmd_q_advance_wrptr(&eth_queues->eth_out_req_cmd_q);

  uint32_t src_addr = (uint32_t)(&(eth_queues->eth_out_req_cmd_q.cmd[eth_wr_index]));
  uint32_t dest_addr = (uint32_t)(&(eth_queues->req_cmd_q[eth_q_id_t::ETH].cmd[eth_wr_index]));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, CMD_SIZE_BYTES/16);

  src_addr = (uint32_t)(&(eth_queues->eth_out_req_cmd_q.wrptr));
  dest_addr = (uint32_t)(&(eth_queues->req_cmd_q[eth_q_id_t::ETH].wrptr));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
}

void resp_fwd_eth(node_cmd_t* resp_cmd, bool from_noc_queue) {

  int32_t remote_resp_index = resp_cmd->src_resp_buf_index;
  int32_t local_resp_index = resp_cmd->local_resp_buf_index;

  if (!node_cmd_error(resp_cmd) && ((resp_cmd->flags & (CMD_DATA_BLOCK | CMD_RD_DATA)) == (CMD_DATA_BLOCK | CMD_RD_DATA)))
  {
    uint32_t num_words = ((resp_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4))) + 0xF ) >> 4;
    uint32_t src_buf_addr = from_noc_queue ? ETH_IN_DATA_BUFFER_ADDR : ETH_DATA_BUFFER_ADDR;
    src_buf_addr += local_resp_index * MAX_BLOCK_SIZE;
    uint32_t dest_buf_addr = ETH_OUT_DATA_BUFFER_ADDR + remote_resp_index * MAX_BLOCK_SIZE;
    eth_send_packet(0, src_buf_addr>>4, dest_buf_addr>>4, num_words);
  }

  cmd_q_t* req_cmd_q = &eth_queues->req_cmd_q[eth_q_id_t::ETH];
  if (resp_cmd->flags & CMD_DEST_UNREACHABLE) {
    req_cmd_q->cmd_counters.error++;
  }

  if (resp_cmd->flags & CMD_WR_ACK) {
    req_cmd_q->cmd_counters.wr_resp++;
  } else if (resp_cmd->flags & CMD_RD_DATA) {
    req_cmd_q->cmd_counters.rd_resp++;
  }

  bool fwd_resp = ((resp_cmd->flags & CMD_BROADCAST) == 0) || (resp_cmd->broadcast_hop_dir == broadcast_direction_t::BCAST_SELF);
  //If broadcasting, only send one response upstream. 
  //Do not forward the responses received for broadcast arcs that originated at local ethernet node.
  if (fwd_resp) {
    uint32_t src_addr = from_noc_queue ? (uint32_t)(&(eth_queues->node_resp_cmd_q[eth_directional_q_id_t::ETH_IN].cmd[local_resp_index])) : (uint32_t)(&(eth_queues->req_cmd_q[eth_q_id_t::ETH].cmd[local_resp_index]));
    uint32_t dest_addr = (uint32_t)(&(eth_queues->node_resp_cmd_q[eth_directional_q_id_t::ETH_OUT].cmd[remote_resp_index]));
    //only need to send first 16 bytes.
    eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
  }
}

void noc_write_aligned_dw(uint32_t dest_noc_x, uint32_t dest_noc_y, uint32_t addr, uint32_t val) {
  uint32_t align_index = (addr % 32) / 4;
  noc_scratch_buf[align_index] = val;
  while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF)){}
  ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                        ((uint32_t)noc_scratch_buf) + 4*align_index,
                        NOC_XY_ADDR(dest_noc_x, dest_noc_y, addr),
                        4,
                        ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
  while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
}

void eth_in_req_cmd_q_rdptr_update() {
  uint32_t src_addr = (uint32_t)(&(eth_queues->req_cmd_q[eth_q_id_t::ETH].rdptr));
  uint32_t dest_addr = (uint32_t)(&(eth_queues->eth_out_req_cmd_q.rdptr));
  eth_send_packet(0, src_addr>>4, dest_addr>>4, 1);
}

uint8_t get_broadcast_hop(uint32_t &broadcast_tag, bool host_queue, broadcast_header_t * bcast_header_ptr)
{
  uint8_t dir = host_queue ? my_node_info->broadcast_state : my_node_info->eth_broadcast_state;
  while (1) {

    if (dir < broadcast_direction_t::BCAST_LEFT) {
      //NONE, SELF -> LEFT
      dir = broadcast_direction_t::BCAST_LEFT;
    } else if (dir == broadcast_direction_t::BCAST_DOWN) {
      //DOWN -> SELF
      dir = broadcast_direction_t::BCAST_SELF;
      broadcast_tag = get_broadcast_tag(my_node_info->my_rack_x, my_node_info->my_rack_y, my_node_info->my_chip_x, my_node_info->my_chip_y);
    } else {
      //LEFT->RIGHT
      //RIGHT->UP
      //UP->DOWN
      dir++;
    }

    if (dir >= broadcast_direction_t::BCAST_LEFT) {
      if (host_queue || (my_node_info->eth_broadcast_skip[dir - broadcast_direction_t::BCAST_LEFT] == 0)) {
        broadcast_tag = my_node_info->broadcast_tags[dir - broadcast_direction_t::BCAST_LEFT];
        if (broadcast_tag & BROADCAST_RACK_XY_LINK) {
          uint32_t dest_rack = broadcast_tag_get_rack(broadcast_tag);
          if (dir == broadcast_direction_t::BCAST_LEFT || dir == broadcast_direction_t::BCAST_RIGHT) {
            //broadcast hop is to another shelf on the same rack.
            //Check if dest shelf has already been visited by this broadcast.
            //If so, we skip sending broadcast request to visited shelf.
            if (bcast_header_ptr->shelf_visited[get_rack_x(dest_rack)] & 0x1 << get_rack_y(dest_rack)) {
              broadcast_tag = -1;
            }
          } else if (dir == broadcast_direction_t::BCAST_UP || dir == broadcast_direction_t::BCAST_DOWN) {
            //broadcast hop is to another shelf on neighboring rack.
            //Check if any shelf on dest rack has been visited.
            //If so, we skip sending broadcast request to dest rack.
            //Dest rack should be serviced by local broadcast propagation
            //as all racks have full shelf to shelf connectivity.
            if (bcast_header_ptr->shelf_visited[get_rack_x(dest_rack)]) {
              broadcast_tag = -1;
            }
          }
        }
        if ((int)broadcast_tag != -1) {
          break;
        }
      }
    } else {
      break;
    }
  }
  return dir;
}

bool req_needs_noc_hop(uint32_t rack, uint64_t sys_addr)
{
  bool req_forward_on_noc = false;
  //Default to false and assume that the command is meant for local chip.
  //It will be completed by current core and does not need to be forwarded
  //over noc or ethernet.
  if (!sys_addr_is_local_chip(rack, sys_addr, my_node_info)) {
    //Command is not addressed to local chip.
    //Now determine if it will get forwarded over ethernet or noc.
    //Do an advance call to get_next_hop_sys_addr() to see where this
    //request is headed.
    uint64_t next_hop_sys_addr = 0;
    uint32_t next_hop_rack = 0;
    eth_route_direction_t direction;
    get_next_hop_sys_addr(0, rack, sys_addr, next_hop_rack, next_hop_sys_addr, direction);
    if (sys_addr_is_local_chip(next_hop_rack, next_hop_sys_addr, my_node_info)) {
      req_forward_on_noc = true;
    }
  }

  return req_forward_on_noc;
}

void host_req_handler() {

  //node_cmd_q_t* node_req_cmd_q = &eth_queues->node_req_cmd_q;
  //node_cmd_q_t* node_resp_cmd_q = &eth_queues->node_resp_cmd_q;

  cmd_q_t* req_cmd_q = &eth_queues->req_cmd_q[eth_q_id_t::HOST];
  cmd_q_t* resp_cmd_q = &eth_queues->resp_cmd_q;

  //skip if transaction issued before have not been flushed yet.
  //if (!ncrisc_noc_reads_flushed(ROUTING_NOC,my_node_info->host_mem_txn_id + NCRISC_ETH_START_TRID)) {
  //  return;
  //}
  while (1) {
    // check if Host request queue empty
    if (!cmd_q_is_empty(req_cmd_q) && !cmd_q_is_full(resp_cmd_q)) {
      //get write pointer to forward host/eth request to node command q.


      uint32_t req_cmd_q_index = req_cmd_q->rdptr.ptr & CMD_BUF_SIZE_MASK;
      routing_cmd_t* req_cmd = req_cmd_q->cmd + req_cmd_q_index;
      uint32_t resp_cmd_q_index = resp_cmd_q->wrptr.ptr & CMD_BUF_SIZE_MASK;
      routing_cmd_t* resp_cmd = resp_cmd_q->cmd + resp_cmd_q_index;

      bool req_forward_on_noc = false;
      if (req_cmd->flags & CMD_BROADCAST) {
        uint32_t temp_broadcast_tag = 0;
        get_broadcast_hop(temp_broadcast_tag, true, (broadcast_header_t *) broadcast_header);
        uint64_t temp_sys_addr_offset = sys_addr_get_offset(req_cmd->sys_addr);
        uint64_t temp_sys_addr = get_sys_addr(broadcast_tag_get_chip_x(temp_broadcast_tag), broadcast_tag_get_chip_y(temp_broadcast_tag), 0x3F, 0x3F, temp_sys_addr_offset);
        uint64_t temp_rack = broadcast_tag_get_rack(temp_broadcast_tag);
        //determine if this broadcast arc needs a noc hop.
        req_forward_on_noc = req_needs_noc_hop(temp_rack, temp_sys_addr);
      } else {
        //determine if this request will be forwarded on noc.
        req_forward_on_noc = req_needs_noc_hop(req_cmd->rack, req_cmd->sys_addr);
      }
      node_cmd_q_t* node_req_cmd_q = &eth_queues->node_req_cmd_q[req_forward_on_noc];
      node_cmd_q_t* node_resp_cmd_q = &eth_queues->node_resp_cmd_q[req_forward_on_noc];

      uint32_t node_wr_ptr;
      if (req_forward_on_noc == false) {
        if (my_node_info->req_pending[eth_q_id_t::HOST] == 0) {
          my_node_info->timestamp = eth_read_wall_clock();

          uint64_t read_addr = NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], (uint32_t)&my_node_info->wr_ptr[eth_q_id_t::HOST]);
          uint64_t wr_ptr_addr = NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], (uint32_t)&node_req_cmd_q->wrptr.ptr);
          while (!ncrisc_noc_fast_read_ok(ROUTING_NOC, NCRISC_SMALL_TXN_CMD_BUF));
          noc_atomic_read_and_increment(ROUTING_NOC, NCRISC_SMALL_TXN_CMD_BUF, wr_ptr_addr, 1, 3, read_addr, false, NCRISC_WR_DEF_TRID);
        }
        node_wr_ptr = my_node_info->wr_ptr[eth_q_id_t::HOST];
      } else {
        node_wr_ptr = node_req_cmd_q->wrptr.ptr;
      }

      //uint32_t node_wr_ptr = my_node_info->wr_ptr[eth_q_id_t::HOST];
      //uint32_t dest_eth_id = my_node_info->my_eth_id;
      //uint32_t full_duplex = my_node_info->eth_port_full_duplex & (0x1 << dest_eth_id);
      // use txn ids of 8 and 9 for host dram read/write.
      if (!node_cmd_q_ptrs_full(node_wr_ptr, node_req_cmd_q->rdptr.ptr) && 
          !node_cmd_q_ptrs_full(node_wr_ptr, node_resp_cmd_q->rdptr.ptr)) {

        uint32_t node_req_cmd_q_index = node_wr_ptr & NODE_CMD_BUF_SIZE_MASK;
        node_cmd_t* node_cmd = node_req_cmd_q->cmd + node_req_cmd_q_index;


        node_cmd->data = req_cmd->data;
        node_cmd->sys_addr = req_cmd->sys_addr;
        node_cmd->timestamp = 0;
        node_cmd->rack = req_cmd->rack;

        bool pop_req = true;
        bool push_resp = false;
        bool wait_flush = false;
        bool inc_counters = true;

        //clear timestamp flag as we dont need to pass it downstream.
        node_cmd->flags = req_cmd->flags & ~CMD_TIMESTAMP;
        node_cmd->broadcast_hop_dir = (uint8_t)broadcast_direction_t::BCAST_NONE;

        if (req_cmd->flags & CMD_DATA_BLOCK_DRAM) {

          uint32_t dram_bytes_processed = my_node_info->dram_bytes_processed;
          uint32_t num_bytes;
          uint32_t broadcast_adjust = 0;
          //If its the first block of a new remote read request we need to push
          //the host dram return address to response q so that when responses start coming back
          //from the network, resp_fwd_host has the dram address where the data needs to be written to.
          if (req_cmd->flags & CMD_RD_REQ) {
            if (dram_bytes_processed == 0) {
              push_resp = true;
              req_cmd->src_resp_buf_index = resp_cmd_q_index;
            }
            node_cmd->src_resp_buf_index = req_cmd->src_resp_buf_index;
          }

          if (req_cmd->flags & CMD_BROADCAST) {
            //read in the broadcast header that has exclusion configuration.
            //this header will be sent out with each outgoing data block.
            broadcast_adjust = BROADCAST_HEADER_SIZE_BYTES;
            if (dram_bytes_processed == 0) {
              while (!ncrisc_noc_fast_read_ok(ROUTING_NOC, NCRISC_SMALL_TXN_CMD_BUF));
              ncrisc_noc_fast_read(ROUTING_NOC, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(0, 3, (0x800000000 | req_cmd->src_addr_tag)), (uint32_t)broadcast_header, BROADCAST_HEADER_SIZE_BYTES, NCRISC_RD_DEF_TRID);
              my_node_info->dram_bytes_processed = BROADCAST_HEADER_SIZE_BYTES;
              dram_bytes_processed = BROADCAST_HEADER_SIZE_BYTES;
              while(!ncrisc_noc_reads_flushed(ROUTING_NOC, NCRISC_RD_DEF_TRID));
              //Set the shelf visited flag for current shelf so that broadcast does not loop back
              //to same shelf from the network.
              broadcast_header_t * bcast_req_header = (broadcast_header_t *) broadcast_header;
              bcast_req_header->shelf_visited[my_node_info->my_rack_x] |= 0x1 << my_node_info->my_rack_y;
            }
            uint32_t broadcast_tag = 0;
            node_cmd->broadcast_hop_dir = get_broadcast_hop(broadcast_tag, true, (broadcast_header_t *) broadcast_header);
            uint64_t sys_addr_offset = sys_addr_get_offset(req_cmd->sys_addr);
            node_cmd->sys_addr = get_sys_addr(broadcast_tag_get_chip_x(broadcast_tag), broadcast_tag_get_chip_y(broadcast_tag), 0x3F, 0x3F, sys_addr_offset);
            node_cmd->rack = broadcast_tag_get_rack(broadcast_tag);
            //determine if this broadcast arc needs a noc hop.
            //req_forward_on_noc = req_needs_noc_hop(node_cmd->rack, node_cmd->sys_addr);
          }

          if ((dram_bytes_processed + MAX_BLOCK_SIZE - broadcast_adjust) >= req_cmd->data) {
            //Last or only reqeust.
            num_bytes = req_cmd->data - dram_bytes_processed;
            if (node_cmd->broadcast_hop_dir == (uint8_t)broadcast_direction_t::BCAST_SELF || node_cmd->broadcast_hop_dir == (uint8_t)broadcast_direction_t::BCAST_NONE) {
              //if broadcasting, this is the last request.
              my_node_info->dram_bytes_processed = 0;
              my_node_info->broadcast_state = (uint8_t)broadcast_direction_t::BCAST_NONE;
            } else {
              my_node_info->broadcast_state = node_cmd->broadcast_hop_dir;
              //Broadcast not finished yet.
              //should not pop host request yet.
              pop_req = false;
            }
            //finished writing all dram data block to remote destination.
            node_cmd->flags |= CMD_LAST_DATA_BLOCK_DRAM;
          } else {
            num_bytes = MAX_BLOCK_SIZE - broadcast_adjust;
            if (node_cmd->broadcast_hop_dir == (uint8_t)broadcast_direction_t::BCAST_SELF || node_cmd->broadcast_hop_dir == (uint8_t)broadcast_direction_t::BCAST_NONE) {
              my_node_info->dram_bytes_processed += MAX_BLOCK_SIZE - broadcast_adjust;
            }
            my_node_info->broadcast_state = node_cmd->broadcast_hop_dir;

            //not done with writing all dram data.
            //should not pop host request yet.
            pop_req = false;
          }

          node_cmd->data = num_bytes + broadcast_adjust;
          node_cmd->sys_addr += dram_bytes_processed - broadcast_adjust;
          //if ((dram_bytes_processed != 0) && ((node_cmd->flags & ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE)) {
          if ((node_cmd->flags & ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE) {
            //If its not the very first block of an ordered dram block write, we do not increment the write request counter.
            //i.e for an ordered dram block write, write counter increments only by 1 for the entire transfer size.
            //dram_bytes_processed == 0 implies its the first block of a multi block transfer, or its the only (first and last) block
            //of this ordered dram block write. i.e the transfer size <= 1024 bytes.
            //inc_counters = false;
            inc_counters = dram_bytes_processed == broadcast_adjust;
          }


          if (req_cmd->flags & CMD_WR_REQ) {
            uint64_t src_buf_addr;
            uint32_t dest_buf_addr = node_req_cmd_q_index * MAX_BLOCK_SIZE + broadcast_adjust;
            dest_buf_addr += req_forward_on_noc ? ETH_IN_DATA_BUFFER_ADDR : ETH_OUT_DATA_BUFFER_ADDR;

            src_buf_addr = req_cmd->src_addr_tag + dram_bytes_processed;
            uint32_t txn_id_index = 1 - my_node_info->host_mem_txn_id;
            node_cmd->host_mem_txn_id = txn_id_index + NCRISC_ETH_START_TRID;
            my_node_info->txn_id_flushed[txn_id_index] = 0;
            while (!ncrisc_noc_fast_read_ok(ROUTING_NOC, NCRISC_SMALL_TXN_CMD_BUF));
            ncrisc_noc_fast_read(ROUTING_NOC, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(0, 3, (0x800000000 | src_buf_addr)), dest_buf_addr, num_bytes, node_cmd->host_mem_txn_id);
            //copy broadcast header while data block is being read from host memory.
            if (broadcast_adjust) {
              volatile uint32_t *dest_buf = (volatile uint32_t *)(dest_buf_addr - broadcast_adjust);
              for (uint32_t i = 0; i < BROADCAST_HEADER_SIZE_BYTES/4; i++) {
                dest_buf[i] = broadcast_header[i];
              }
            }
            if (node_cmd->flags & CMD_LAST_DATA_BLOCK_DRAM) {
              //if its the last block, we need to wait for flush before incrementing request command q read pointer.
              while (!ncrisc_noc_reads_flushed(ROUTING_NOC,node_cmd->host_mem_txn_id));
              my_node_info->txn_id_flushed[txn_id_index] = 1;
              if (broadcast_adjust && pop_req == true) {
                //last broadcast packet sent.
                //reset broadcast header.
                for (uint32_t i = 0; i < BROADCAST_HEADER_SIZE_BYTES/4; i++) {
                  broadcast_header[i] = 0;
                }
              }
            }
          }
        } else if ((req_cmd->flags & (CMD_DATA_BLOCK | CMD_WR_REQ)) == (CMD_DATA_BLOCK | CMD_WR_REQ)) {

          uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
          uint32_t src_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + req_cmd_q_index * MAX_BLOCK_SIZE;
          uint32_t dest_buf_addr = node_req_cmd_q_index * MAX_BLOCK_SIZE;
          dest_buf_addr += req_forward_on_noc ? ETH_IN_DATA_BUFFER_ADDR : ETH_OUT_DATA_BUFFER_ADDR;
          if (req_cmd->flags & CMD_BROADCAST) {
            //read in the broadcast header that has exclusion configuration.
            //this header will be sent out with each outgoing data block.
            uint32_t broadcast_tag = 0;
            node_cmd->broadcast_hop_dir = get_broadcast_hop(broadcast_tag, true, (broadcast_header_t *) src_buf_addr);
            uint64_t sys_addr_offset = sys_addr_get_offset(req_cmd->sys_addr);
            node_cmd->sys_addr = get_sys_addr(broadcast_tag_get_chip_x(broadcast_tag), broadcast_tag_get_chip_y(broadcast_tag), 0x3F, 0x3F, sys_addr_offset);
            node_cmd->rack = broadcast_tag_get_rack(broadcast_tag);
            //Set the shelf visited flag for current shelf so that broadcast does not loop back
            //to same shelf from the network.
            broadcast_header_t * bcast_req_header = (broadcast_header_t *) src_buf_addr;
            bcast_req_header->shelf_visited[my_node_info->my_rack_x] |= 0x1 << my_node_info->my_rack_y;
            pop_req = false;
          }
          if (node_cmd->broadcast_hop_dir == (uint8_t)broadcast_direction_t::BCAST_SELF) {
              pop_req = true;
              my_node_info->broadcast_state = (uint8_t)broadcast_direction_t::BCAST_NONE;
          } else {
            my_node_info->broadcast_state = node_cmd->broadcast_hop_dir;
          }


          while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
          ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                            src_buf_addr,
                            NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], dest_buf_addr),
                            num_bytes,
                            ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
          wait_flush = true;
        } else if (req_cmd->flags & CMD_RD_REQ) {
          //For a plain d-word read request or block read request, need to push to respose q to
          //reserve a response slot. For CMD_DATA_BLOCK_DRAM Reads, push_resp is set to true
          //under if (req_cmd->flags & CMD_DATA_BLOCK_DRAM).
          push_resp = true;
          req_cmd->src_addr_tag = 0;
          node_cmd->src_resp_buf_index = resp_cmd_q_index;
        }


        node_cmd->src_resp_q_id = eth_q_id_t::HOST;

#ifdef ENABLE_TIMESTAMPING
        //time stamping section.
        //timestamps are in AICLK cycles.
        uint32_t ts_active = my_node_info->timestamp_active;
        uint32_t ts_index = my_node_info->timestamp_index;
        if (req_cmd->flags & CMD_TIMESTAMP) {
          ts_active = 1;
          ts_index = 0;
        }

        if (ts_active) {
          routing_cmd_timestamp[ts_index] = my_node_info->timestamp;
          //set bit 7 to 1 to indicate a valid index. when handling the response for this request.
          //for last data block dram, ts_index should be 0 since we have one timestamp for the whole transfer.
          //response handler will use routing_cmd_timestamp[0] as the start time when calculating time diff.
          node_cmd->timestamp = node_cmd->flags & CMD_LAST_DATA_BLOCK_DRAM ? 0x80 : (0x80 | ts_index);
          if ((ts_index >= NODE_CMD_BUF_SIZE -1) || (node_cmd->flags & CMD_LAST_DATA_BLOCK_DRAM)) {
            ts_active = 0;
            ts_index = 0;
          } else {
            if (node_cmd->flags & CMD_DATA_BLOCK_DRAM) {
              //for data block dram mode, we keep one timestamp for the whole transfer.
              //strating time for this mode is in routing_cmd_timestamp[0]
              //set ts_index for all intermediate transfers to 1 so that we do not overwrite routing_cmd_timestamp[0]
              ts_index = 1;
            } else {
              ts_index++;
            }
          }

          //if my_node_info->time_stamp_active == 0, I do not need to clear node_cmd->timestamp to 0.
          //its already cleared when req_cmd got copied to node_cmd, since host never sets this byte to 1
          //in its request queue.
          my_node_info->timestamp_active = ts_active;
          my_node_info->timestamp_index = ts_index;
        }
#endif
        // clear timestamp start trigger.
        // we dont want it to keep starting timestamp counter in case its a CMD_DATA_BLOCK_DRAM
        req_cmd->flags &= ~CMD_TIMESTAMP;
        node_cmd->Valid = 1;
        my_node_info->req_pending[eth_q_id_t::HOST] = 0;
        if (inc_counters && (req_cmd->flags & CMD_WR_REQ)) {
          req_cmd_q->cmd_counters.wr_req++;
        } else if (req_cmd->flags & CMD_RD_REQ) {
          req_cmd_q->cmd_counters.rd_req++;
        }
        if (push_resp) {
          resp_cmd->sys_addr = req_cmd->sys_addr;
          resp_cmd->flags = 0;
          resp_cmd->data = 0;
          resp_cmd->src_addr_tag = req_cmd->src_addr_tag;
          cmd_q_advance_wrptr(resp_cmd_q);
        }
        if (pop_req) {
          if (wait_flush) {
            while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
            //wait_write_flush(ROUTING_NOC, NCRISC_WR_DEF_TRID, 0xded30000);
          }
          cmd_q_advance_rdptr(req_cmd_q);
        }
        if (req_forward_on_noc) {
          //This request needs a noc hop.
          //break the request processing loop as we need to make sure
          //that dest noc queue has accepted this request before inserting next
          //request in local noc queue.
          my_node_info->noc_fwd_counter++;
          node_cmd_q_advance_wrptr(node_req_cmd_q);
          //break;
        }

      } else {
        if (req_forward_on_noc == false) {
          if (my_node_info->req_pending[eth_q_id_t::HOST] == 0) {
            my_node_info->req_pending[eth_q_id_t::HOST] = 1;
            //my_node_info->host_mem_txn_id ^= 1;
          }
        }
        break;
      }

    } else {
      //host request command q is empty or host response queue is full
      break;
    }
  }
}

void eth_req_handler() {

  node_cmd_q_t* node_req_cmd_q = &eth_queues->node_req_cmd_q[eth_directional_q_id_t::ETH_IN];
  node_cmd_q_t* node_resp_cmd_q = &eth_queues->node_resp_cmd_q[eth_directional_q_id_t::ETH_IN];

  while (1) {
    cmd_q_t* req_cmd_q = &eth_queues->req_cmd_q[eth_q_id_t::ETH];

    // check if Host/Eth request queue empty
    if (!cmd_q_is_empty(req_cmd_q)) {

      uint32_t req_cmd_q_index = req_cmd_q->rdptr.ptr & CMD_BUF_SIZE_MASK;
      routing_cmd_t* req_cmd = req_cmd_q->cmd + req_cmd_q_index;

      uint64_t cmd_dest_sys_addr = req_cmd->sys_addr;
      uint32_t rack = req_cmd->rack;
      uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;

      if ((req_cmd->flags & CMD_BROADCAST) == 0 && sys_addr_is_local_chip(rack, cmd_dest_sys_addr, my_node_info)) {
        node_cmd_t * node_cmd = (node_cmd_t *)req_cmd;
        uint32_t src_resp_buf_index = node_cmd->src_resp_buf_index;
        node_cmd->src_resp_buf_index = req_cmd_q_index;
        node_cmd->local_resp_buf_index = req_cmd_q_index;
        if (sys_addr_is_local(noc_id, rack, cmd_dest_sys_addr, my_node_info)) {
          // perform local read/write and store the response into the resp queue, no forwarding
          cmd_service_local(node_cmd, node_cmd);
        } else {
          //do a noc copy for local chip access
          cmd_service_noc(node_cmd, node_cmd);
        }
        node_cmd->src_resp_buf_index = src_resp_buf_index;
        //Since we are servicing node_cmd in place, 
        //req_cmd->flags now holds response flags.
        if (req_cmd->flags & CMD_WR_ACK) {
          req_cmd_q->cmd_counters.wr_req++;
        } else if (req_cmd->flags & CMD_RD_DATA) {
          req_cmd_q->cmd_counters.rd_req++;
        }
        if (req_cmd->flags == ORDERED_DRAM_BLOCK_WRITE_ACK) {
          req_cmd_q->cmd_counters.wr_resp++;
        } else {
          //Response is forwarded only if its not a late flush write.
          resp_fwd_eth(node_cmd, false);
        }
        cmd_q_advance_rdptr(req_cmd_q);
        eth_in_req_cmd_q_rdptr_update();
        continue;
      }

      uint32_t node_wr_ptr = node_req_cmd_q->wrptr.ptr;
      bool wait_flush = false;
      if (!node_cmd_q_ptrs_full(node_wr_ptr, node_req_cmd_q->rdptr.ptr) &&
          !node_cmd_q_ptrs_full(node_wr_ptr, node_resp_cmd_q->rdptr.ptr)) {

        uint32_t node_req_cmd_q_index = node_wr_ptr & NODE_CMD_BUF_SIZE_MASK;
        node_cmd_t* node_cmd = node_req_cmd_q->cmd + node_req_cmd_q_index;

        uint32_t req_cmd_q_index = req_cmd_q->rdptr.ptr & CMD_BUF_SIZE_MASK;
        routing_cmd_t* req_cmd = req_cmd_q->cmd + req_cmd_q_index;
        req_cmd->src_resp_q_id = eth_q_id_t::ETH;
        bool pop_req = true;

        uint32_t *dest = (uint32_t *)node_cmd;
        uint32_t *src = (uint32_t *)req_cmd;
        for (uint32_t i = 0; i < CMD_SIZE_BYTES/4; i++) {
          dest[i] = src[i];
        }

        uint32_t src_buf_addr = ETH_ROUTING_DATA_BUFFER_ADDR + 4096 + req_cmd_q_index * MAX_BLOCK_SIZE;

        if (req_cmd->flags & CMD_BROADCAST) {
          //read in the broadcast header that has exclusion configuration.
          //this header will be sent out with each outgoing data block.
          uint32_t broadcast_tag = 0;
          node_cmd->broadcast_hop_dir = get_broadcast_hop(broadcast_tag, false, (broadcast_header_t *)src_buf_addr);
          uint64_t sys_addr_offset = sys_addr_get_offset(req_cmd->sys_addr);
          node_cmd->sys_addr = get_sys_addr(broadcast_tag_get_chip_x(broadcast_tag), broadcast_tag_get_chip_y(broadcast_tag), 0x3F, 0x3F, sys_addr_offset);
          node_cmd->rack = broadcast_tag_get_rack(broadcast_tag);
          //Set the shelf visited flag for current shelf so that broadcast does not loop back
          //to same shelf from the network.
          broadcast_header_t * bcast_req_header = (broadcast_header_t *) src_buf_addr;
          bcast_req_header->shelf_visited[my_node_info->my_rack_x] |= 0x1 << my_node_info->my_rack_y;
          pop_req = false;
        }
        if (node_cmd->broadcast_hop_dir == (uint8_t)broadcast_direction_t::BCAST_SELF) {
          pop_req = true;
          my_node_info->eth_broadcast_state = (uint8_t)broadcast_direction_t::BCAST_NONE;
        } else {
          my_node_info->eth_broadcast_state = node_cmd->broadcast_hop_dir;
        }

        if ((req_cmd->flags & (CMD_DATA_BLOCK | CMD_WR_REQ)) == (CMD_DATA_BLOCK | CMD_WR_REQ))
        {
          uint32_t num_bytes = req_cmd->data & (MAX_BLOCK_SIZE | (MAX_BLOCK_SIZE - 4));
          uint32_t dest_buf_addr = ETH_IN_DATA_BUFFER_ADDR + node_req_cmd_q_index * MAX_BLOCK_SIZE;
          while (!ncrisc_noc_fast_write_ok(ROUTING_NOC, NCRISC_WR_CMD_BUF));
          ncrisc_noc_fast_write(ROUTING_NOC, NCRISC_WR_CMD_BUF,
                              src_buf_addr,
                              NOC_XY_ADDR(my_node_info->my_noc_x[0], my_node_info->my_noc_y[0], dest_buf_addr),
                              num_bytes,
                              ROUTING_VC, false, false, 1, NCRISC_WR_DEF_TRID);
          wait_flush = true;
        }

        //clear timestamp flag as we dont need to pass it downstream.
        node_cmd->flags &= ~CMD_TIMESTAMP;
        node_cmd->Valid = 1;
        node_cmd_q_advance_wrptr(node_req_cmd_q);

        if (req_cmd->flags & CMD_WR_REQ) {
          req_cmd_q->cmd_counters.wr_req++;
          if ((req_cmd->flags & LAST_ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE) {
            //if not last ordered dram block write, inc response coutner as well since there will be no downstream response.
            //if last ordered dram block write, we will get a response and the counter will increment when resp_fwd_eth()
            //gets called.
            req_cmd_q->cmd_counters.wr_resp++;
          }
        } else if (req_cmd->flags & CMD_RD_REQ) {
          req_cmd_q->cmd_counters.rd_req++;
        }
        
        if (pop_req) {
          if (wait_flush) {
            while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
            //wait_write_flush(ROUTING_NOC, NCRISC_WR_DEF_TRID, 0xded40000);
          }
          cmd_q_advance_rdptr(req_cmd_q);
          eth_in_req_cmd_q_rdptr_update();
        }
      } else {
        // Node In command queue is full.
        break;
      }
    }
    else {
      //eth request command q is empty.
      break;
    }
  }
}

void eth_routing_handler() {

  volatile uint32_t *post_code = (volatile uint32_t *) 0xffb3010c;
  *post_code = 0x01000000;
  host_req_handler();
  *post_code = 0x01010000;
  eth_req_handler();
  *post_code = 0x01020000;

  while (!ncrisc_noc_nonposted_writes_flushed(ROUTING_NOC, NCRISC_WR_DEF_TRID));
  //wait_write_flush(ROUTING_NOC, NCRISC_WR_DEF_TRID, 0xded50000);
  *post_code = 0x01030000;

  for (int q_id = 0; q_id < eth_directional_q_id_t::NUM_ETHQ; q_id++) {
  node_cmd_q_t* node_req_cmd_q = &eth_queues->node_req_cmd_q[q_id];
  node_cmd_q_t* node_resp_cmd_q = &eth_queues->node_resp_cmd_q[q_id];
  *post_code = 0x01040000;

  while (1) {
    *post_code = 0x01050000;
    if (!node_cmd_q_is_empty(node_req_cmd_q) && !node_cmd_q_is_full(node_resp_cmd_q) && node_cmd_valid(node_req_cmd_q)) {

      uint32_t req_cmd_q_index = node_req_cmd_q->rdptr.ptr & NODE_CMD_BUF_SIZE_MASK;
      uint32_t resp_cmd_q_index = node_resp_cmd_q->wrptr.ptr & NODE_CMD_BUF_SIZE_MASK;
      node_cmd_t* req_cmd = node_req_cmd_q->cmd + req_cmd_q_index;
      node_cmd_t* resp_cmd = node_resp_cmd_q->cmd + resp_cmd_q_index;

      if (req_cmd->flags & CMD_DATA_BLOCK)
      {
        // if data block mode, data field contians number of bytes to move.
        // max size = MAX_BLOCK_SIZE
        if (req_cmd->data > MAX_BLOCK_SIZE)
        {
          req_cmd->data = MAX_BLOCK_SIZE;
        }
      }

      if ((req_cmd->src_resp_q_id == eth_q_id_t::HOST) && (req_cmd->flags & (CMD_DATA_BLOCK_DRAM | CMD_WR_REQ)) == (CMD_DATA_BLOCK_DRAM | CMD_WR_REQ)) {
        uint32_t txn_id_index = req_cmd->host_mem_txn_id - NCRISC_ETH_START_TRID;
        my_node_info->host_mem_txn_id = txn_id_index;
        if (my_node_info->txn_id_flushed[txn_id_index] == 0) {
          while (!ncrisc_noc_reads_flushed(ROUTING_NOC,req_cmd->host_mem_txn_id)) {
            //Dram reads for this block not yet flushed.
            //break;
            *post_code = 0x01050001;
          }
          my_node_info->txn_id_flushed[txn_id_index] = 1;
        }
      }

      resp_cmd->sys_addr = req_cmd->sys_addr;
      resp_cmd->flags = 0;
      resp_cmd->resp_forwarded_ooo = 0;
      resp_cmd->src_resp_q_id = req_cmd->src_resp_q_id;
      resp_cmd->src_resp_buf_index = req_cmd->src_resp_buf_index;
      resp_cmd->local_resp_buf_index = resp_cmd_q_index;
      resp_cmd->src_noc_x = req_cmd->src_noc_x;
      resp_cmd->src_noc_y = req_cmd->src_noc_y;
      resp_cmd->timestamp = req_cmd->timestamp;
      resp_cmd->broadcast_hop_dir = req_cmd->broadcast_hop_dir;

      req_cmd->src_resp_buf_index = resp_cmd_q_index;
      req_cmd->timestamp = 0;

      uint64_t cmd_dest_sys_addr = req_cmd->sys_addr;
      uint32_t rack = req_cmd->rack;
      uint32_t noc_id = (req_cmd->flags & CMD_NOC_ID) >> CMD_NOC_ID_SHIFT;

      if (sys_addr_is_local(noc_id, rack, cmd_dest_sys_addr, my_node_info)) {
        *post_code = 0x01050002;
        // perform local read/write and store the response into the resp queue, no forwarding
        cmd_service_local(req_cmd, resp_cmd);
        node_cmd_q_advance_wrptr(node_resp_cmd_q);
        node_cmd_q_advance_rdptr(node_req_cmd_q);
      } else {
        if (sys_addr_is_local_chip(rack, cmd_dest_sys_addr, my_node_info)) {
          *post_code = 0x01050003;
          //do a noc copy for local chip access
          if (req_cmd->flags & CMD_BROADCAST) {
            cmd_service_broadcast_noc(q_id, req_cmd, resp_cmd);
          } else {
            cmd_service_noc(req_cmd, resp_cmd);
          }
          node_cmd_q_advance_wrptr(node_resp_cmd_q);
          node_cmd_q_advance_rdptr(node_req_cmd_q);
        } else {
          *post_code = 0x01050004;
          //need to route over ethernet.
          //figure out which ethernet core to forward this request to.
          uint64_t next_hop_sys_addr = 0;
          uint32_t next_hop_rack = 0;
          eth_route_direction_t direction;
          get_next_hop_sys_addr(req_cmd->flags & CMD_ORDERED, rack, cmd_dest_sys_addr, next_hop_rack, next_hop_sys_addr, direction);

          if ((int64_t) next_hop_sys_addr == -1) {
            *post_code = 0x01050005;
            //destination is unreachable. return error.
            cmd_service_error(req_cmd, resp_cmd);
            node_cmd_q_advance_wrptr(node_resp_cmd_q);
            node_cmd_q_advance_rdptr(node_req_cmd_q);
          } else {
            if (sys_addr_is_local_chip(next_hop_rack, next_hop_sys_addr, my_node_info)) {
              *post_code = 0x01050006;
              //do atomic incremet to get next write pointer on remote noc core.
              if (req_fwd_noc(req_cmd, next_hop_sys_addr)) {
                if ((req_cmd->flags & LAST_ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE) {
                  //this is a posted write with late ack.
                  //ack will be sent with last dram block write.
                  //set resp flag to non-zero as no response will be forwarded by downstream nodes.
                  resp_cmd->flags = ORDERED_DRAM_BLOCK_WRITE_ACK;
                }
                node_cmd_q_advance_wrptr(node_resp_cmd_q);
                node_cmd_q_advance_rdptr(node_req_cmd_q);
                if ((req_cmd->flags & CMD_ORDERED) == 0) {
                  inc_eth_txn_counter(direction);
                }
              } else {
                //since the request did not get forwarded,
                //restore the src resp buf index to sending node's buf index.
                //will try forwarding again in next iteration of routing handler.
                req_cmd->src_resp_buf_index = resp_cmd->src_resp_buf_index;
                req_cmd->timestamp = resp_cmd->timestamp;
                break;
              }
            } else {
              if (!cmd_q_is_full(&eth_queues->eth_out_req_cmd_q)) {
                *post_code = 0x01050007;
                req_fwd_eth((routing_cmd_t *)req_cmd);
                if ((req_cmd->flags & LAST_ORDERED_DRAM_BLOCK_WRITE) == ORDERED_DRAM_BLOCK_WRITE) {
                  //this is a posted write with late ack.
                  //ack will be sent with last dram block write.
                  //set resp flag to non-zero as no response will be forwarded by downstream nodes.
                  resp_cmd->flags = ORDERED_DRAM_BLOCK_WRITE_ACK;
                }
                node_cmd_q_advance_wrptr(node_resp_cmd_q);
                node_cmd_q_advance_rdptr(node_req_cmd_q);
              } else {
                *post_code = 0x01050008;
                //since the request did not get forwarded,
                //restore the src resp buf index to sending node's buf index.
                //will try forwarding again in next iteration of routing handler.
                req_cmd->src_resp_buf_index = resp_cmd->src_resp_buf_index;
                req_cmd->timestamp = resp_cmd->timestamp;
                break;
              }
            }
          }
        }
      }
    } else {
      //node req cmd q empty, or node resp cmd q full or node cmd not valid.
      break;
    }
  }

  // Check Node Response Command Q
  uint32_t resp_ooo = 0;
  uint32_t resp_cmd_q_rd_ptr = node_resp_cmd_q->rdptr.ptr;
  *post_code = 0x01060000;

  while (1) {
    *post_code = 0x01070000;

    if (!node_cmd_q_is_empty(node_resp_cmd_q)) {
      uint32_t resp_cmd_q_rd_index = resp_cmd_q_rd_ptr & NODE_CMD_BUF_SIZE_MASK;
      if (cmd_q_ptrs_empty(node_resp_cmd_q->wrptr.ptr, resp_cmd_q_rd_ptr)) {
        break;
      }
      resp_ooo = resp_cmd_q_rd_ptr != node_resp_cmd_q->rdptr.ptr ? 1 : 0;
      node_cmd_t* resp_cmd = node_resp_cmd_q->cmd + resp_cmd_q_rd_index;
      if (resp_cmd->flags != 0) {
        //req src addr shoudl be pulled from resp q entry.
        if (resp_cmd->flags == ORDERED_DRAM_BLOCK_WRITE_ACK) {
          *post_code = 0x01070001;
          if (resp_ooo == 0) {
            //late posted writes. 
            //response will be sent only for last ordered dram block write.
            //increment response read pointer since its a dummy response.
            node_cmd_q_advance_rdptr(node_resp_cmd_q);
            resp_cmd_q_rd_ptr = node_resp_cmd_q->rdptr.ptr;
          } else {
            node_advance_rdptr(resp_cmd_q_rd_ptr);
          }
        } else if (resp_cmd->src_resp_q_id == eth_q_id_t::HOST) {
          *post_code = 0x01070002;
          if (resp_ooo == 0) {
            cmd_q_t* host_resp_cmd_q = &eth_queues->resp_cmd_q;
            if (!cmd_q_is_full(host_resp_cmd_q)) {
              resp_fwd_host(q_id, resp_cmd);
              node_cmd_q_advance_rdptr(node_resp_cmd_q);
              resp_cmd_q_rd_ptr = node_resp_cmd_q->rdptr.ptr;
            } else {
              //host resp q is full
              break;
            }
          } else {
            node_advance_rdptr(resp_cmd_q_rd_ptr);
          }
        }
        else if (resp_cmd->src_resp_q_id == eth_q_id_t::NOC) {
          *post_code = 0x01070003;
          if (resp_cmd->resp_forwarded_ooo == 0) {
            resp_fwd_noc(resp_cmd);
            resp_cmd->resp_forwarded_ooo = resp_ooo;
          }
          if (resp_ooo == 0) {
            node_cmd_q_advance_rdptr(node_resp_cmd_q);
            resp_cmd_q_rd_ptr = node_resp_cmd_q->rdptr.ptr;
          } else {
            node_advance_rdptr(resp_cmd_q_rd_ptr);
          }
        } else if (resp_cmd->src_resp_q_id == eth_q_id_t::ETH) {
          *post_code = 0x01070004;
          if (((resp_cmd->flags & CMD_BROADCAST) != 0) && resp_ooo) {
              //Broadcast response cannot be sent out of order.
              //We need for all broadcast arcs originating from this node to have responded
              //before we can respond to upstream broadcast sender.
              node_advance_rdptr(resp_cmd_q_rd_ptr);
          } else {
            if (resp_cmd->resp_forwarded_ooo == 0) {
              resp_fwd_eth(resp_cmd, true);
              resp_cmd->resp_forwarded_ooo = resp_ooo;
            }
            if (resp_ooo == 0) {
              node_cmd_q_advance_rdptr(node_resp_cmd_q);
              resp_cmd_q_rd_ptr = node_resp_cmd_q->rdptr.ptr;
            } else {
              node_advance_rdptr(resp_cmd_q_rd_ptr);
            }
          }
        }
        //Also check for host q id above. This if/else should be on src resp q id instead of address.

      } else {
        //resp_ooo = true;
        *post_code = 0x01070005;
        node_advance_rdptr(resp_cmd_q_rd_ptr);
      }
    } else {
      //node response command q is empty
      *post_code = 0x01070006;
      break;
    }
  }
  *post_code = 0x01080000;
  }
  *post_code = 0x01090000;
}

void eth_link_status_check(void) {
  volatile uint32_t *post_code = (volatile uint32_t *) 0xffb3010c;
  if ((!my_node_info->eth_node) || (my_node_info->train_status != LINK_TRAIN_SUCCESS)) return;

  *post_code = 0x03010000;
  uint32_t pcs_status = eth_pcs_ind_reg_read(0x30001); // rx link up, should get 0x6
  uint32_t pcs_faults = eth_pcs_ind_reg_read(0x30008);
  uint32_t crc_err = eth_mac_reg_read(0x928);
  test_results[5] = pcs_status;
  test_results[47] = crc_err;
  test_results[49] = pcs_faults;

  uint64_t total_corr_cw = ((uint64_t)test_results[52] << 32) | (uint64_t)test_results[53];
  uint64_t total_uncorr_cw = ((uint64_t)test_results[54] << 32) | (uint64_t)test_results[55];

  uint32_t corr_cw_lo = eth_pcs_ind_reg_read(0x100ca);
  uint32_t corr_cw_hi = eth_pcs_ind_reg_read(0x100cb);
  uint32_t corr_cw = (corr_cw_hi << 16) + corr_cw_lo;
  total_corr_cw += (uint64_t)corr_cw;

  test_results[52] = total_corr_cw >> 32;
  test_results[53] = total_corr_cw & 0xFFFFFFFF;

  uint32_t uncorr_cw_lo = eth_pcs_ind_reg_read(0x100cc);
  uint32_t uncorr_cw_hi = eth_pcs_ind_reg_read(0x100cd);
  uint32_t uncorr_cw = (uncorr_cw_hi << 16) + uncorr_cw_lo;
  total_uncorr_cw += (uint64_t)uncorr_cw;

  test_results[54] = total_uncorr_cw >> 32;
  test_results[55] = total_uncorr_cw & 0xFFFFFFFF;

  if (!config_buf->link_status_check_en) return;

  bool restart_link = false;

  uint64_t curr_time = eth_read_wall_clock();
  *post_code = 0x03020000;

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
  *post_code = 0x03030000;

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
    *post_code = 0x03040000;
    link_train_status_e train_status;
    do {
      for (uint32_t i = 0; i < (DEBUG_BUF_SIZE)/4; i++) {
        debug_buf[i] = 0;
      }

      // Clear dummy packet handshake data
      for (uint32_t i = 16; i < 24; i++) {
        test_results[i] = 0;
      }

      *post_code = 0x03050000;
      train_status = eth_link_start_handler(config_buf, LINK_PCS_RESET, true);
      my_node_info->train_status = train_status;
      test_results[7]++;
      *post_code = 0x03060000;
    } while (my_node_info->train_status != LINK_TRAIN_SUCCESS);

    mac_rcv_fail = 0;
    test_results[5] = 0;
    test_results[6] = 0;
    // test_results[14] = mac_rcv_fail;
    test_results[15] = 0;
  }
  *post_code = 0x03070000;
}
