// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _ETH_ROUTING_H_

#include <stdint.h>
#include "eth_interface.h"
#include "eth_init.h"

const uint32_t ETH_DATA_BUFFER_ADDR = 0x13000;
//const uint32_t NOC_DATA_BUFFER_ADDR = 0x14000;
const uint32_t ETH_IN_DATA_BUFFER_ADDR = 0x16000;
const uint32_t ETH_OUT_DATA_BUFFER_ADDR = 0x14000;
//const uint32_t ETH_DATA_BUFFER_ADDR = eth_l1_address_map::address_map::DATA_BUFFER_BASE + eth_l1_address_map::address_map::DATA_BUFFER_SIZE_HOST;
//const uint32_t NOC_DATA_BUFFER_ADDR = ETH_DATA_BUFFER_ADDR + eth_l1_address_map::address_map::DATA_BUFFER_SIZE_ETH;

//shelf to shelf connections on a board.
#define MAX_BOARD_CONNECTIONS_Y 8
//rack to rack connection son a board.
#define MAX_BOARD_CONNECTIONS_X 4

#define MAX_EDGE_ROUTERS (MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2)

//Galaxy rack has mas 10 shelves.
//8 Galaxy boards + 2 Host shelves.
#define MAX_RACK_SHELVES 10
//#define RACK_LEFT 0
//#define RACK_RIGHT 1

typedef enum {
 BCAST_NONE = 0,
 BCAST_SELF = 1,
 BCAST_LEFT = 2,
 BCAST_RIGHT = 3,
 BCAST_UP = 4,
 BCAST_DOWN = 5
} broadcast_direction_t;

typedef enum {
 LEFT = 0,
 RIGHT = 1,
 UP = 2,
 DOWN = 3,
 RACK_LEFT = 4,
 RACK_RIGHT = 5,
 RACK_UP = 6,
 RACK_DOWN = 7,
 NUM = 4,
 COUNT = 8
} eth_route_direction_t;

typedef enum {
 ETH_UNKNOWN = 0,
 ETH_UNCONNECTED = 1,
 ETH_CONN_L = 2,
 ETH_CONN_R = 3,
 ETH_CONN_U = 4,
 ETH_CONN_D = 5,
 ETH_RACK_L = 6,
 ETH_RACK_R = 7,
 ETH_RACK_U = 8,
 ETH_RACK_D = 9
} eth_conn_info_t;

typedef enum {
 HOST = 0,
 ETH = 1,
 NOC = 2,
 NUM_Q = 3
} eth_q_id_t;

typedef enum {
 ETH_OUT = 0,
 ETH_IN = 1,
 NUM_ETHQ = 2
} eth_directional_q_id_t;

typedef enum {
 PHASE_0 = 0,
 PHASE_1 = 1,
 PHASE_2 = 2,
 PHASE_3 = 3,
 PHASE_4 = 4,
 PHASE_5 = 5
} routing_discovery_phase_t;

const uint32_t NODE_CMD_BUF_SIZE = 8;  // we assume must be 2^N
const uint32_t NODE_CMD_BUF_SIZE_MASK = (NODE_CMD_BUF_SIZE - 1);
const uint32_t NODE_CMD_BUF_PTR_MASK  = ((NODE_CMD_BUF_SIZE << 1) - 1);
const uint32_t TIMESTAMPS_COUNT = 16;  // we assume must be 2^N

const uint32_t NODE_CMD_SIZE_BYTES = 32;
typedef struct {
  uint64_t sys_addr;
  uint32_t data;
  uint32_t flags;
  uint16_t rack;
  uint16_t src_resp_buf_index;
  uint8_t  src_noc_x;
  uint8_t  src_noc_y;
  uint8_t  local_resp_buf_index;
  uint8_t  timestamp;
  uint8_t  src_resp_q_id;
  uint8_t  host_mem_txn_id;
  uint8_t  broadcast_hop_dir;
  uint8_t  resp_forwarded_ooo;
  uint32_t Valid;
} node_cmd_t;
static_assert(sizeof(node_cmd_t) == NODE_CMD_SIZE_BYTES);

const uint32_t NODE_CMD_Q_SIZE_BYTES = 2*REMOTE_UPDATE_PTR_SIZE_BYTES + CMD_COUNTERS_SIZE_BYTES + NODE_CMD_BUF_SIZE*NODE_CMD_SIZE_BYTES;
typedef struct {
  cmd_counters_t cmd_counters;
  remote_update_ptr_t wrptr;
  remote_update_ptr_t rdptr;
  node_cmd_t cmd[NODE_CMD_BUF_SIZE];
} node_cmd_q_t;
static_assert(sizeof(node_cmd_q_t) == NODE_CMD_Q_SIZE_BYTES);

const uint32_t ETH_QUEUES_SIZE_BYTES = CMD_Q_SIZE_BYTES * 4 + NODE_CMD_Q_SIZE_BYTES * 4 + 8 * TIMESTAMPS_COUNT;

typedef struct {
  uint64_t cmd_latency[TIMESTAMPS_COUNT];
  cmd_q_t req_cmd_q[eth_q_id_t::NUM_Q-1];
  cmd_q_t resp_cmd_q;
  cmd_q_t eth_out_req_cmd_q;
  node_cmd_q_t node_req_cmd_q[eth_directional_q_id_t::NUM_ETHQ];
  node_cmd_q_t node_resp_cmd_q[eth_directional_q_id_t::NUM_ETHQ];
} eth_queues_t;

static_assert(sizeof(eth_queues_t) == ETH_QUEUES_SIZE_BYTES);
static_assert(sizeof(eth_queues_t) <= 2176);

inline uint8_t get_noc_x(uint8_t x, uint8_t noc_id) {
  return (noc_id == 0) ? x : (NOC_SIZE_X - 1 - x);
}


inline uint8_t get_noc_y(uint8_t y, uint8_t noc_id) {
  return (noc_id == 0) ? y : (NOC_SIZE_Y - 1 - y);
}

inline uint32_t eth_id_get_x(uint32_t eth_id, uint32_t noc_id = 0) {  
  uint32_t result;
  if ((eth_id % 2) == 1) {
    result = 1 + ((eth_id % 8) / 2);
  }  else {
    result = 9 - ((eth_id % 8) / 2);
  }
  return get_noc_x(result, noc_id);
}

inline uint32_t eth_id_get_y(uint32_t eth_id, uint32_t noc_id = 0) {
  uint32_t result = (eth_id > 7) ? 6 : 0;
  return get_noc_y(result, noc_id);
}


inline bool node_is_eth(uint32_t noc_x, uint32_t noc_y) {
  return ((noc_y == 0) || (noc_y == 6)) && (noc_x != 0) && (noc_x != 5);
}


inline bool node_is_tensix(uint32_t noc_x, uint32_t noc_y) {
  return (noc_y != 0) && (noc_y != 6) && (noc_x != 0) && (noc_x != 5);
}


inline uint64_t get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint64_t offset) {
  uint64_t result = chip_y;
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= chip_x; 
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= noc_y; 
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= noc_x; 
  result <<= NOC_ADDR_LOCAL_BITS;
  result |= offset;
  return result;
}

inline uint32_t get_sys_addr_tag(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y) {
  uint32_t result = chip_y;
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= chip_x; 
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= noc_y; 
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= noc_x; 
  return result;
}

#define BROADCAST_TAG_RACK_MASK  0xFFFF
#define BROADCAST_RACK_XY_LINK  0x80000000 //identifies if a broadcast link is to another shelf. other shelf can be on same or different rack.
inline uint32_t get_broadcast_tag(uint32_t rack_x, uint32_t rack_y, uint32_t chip_x, uint32_t chip_y) {
  uint32_t result = rack_y;
  result <<= CHIP_COORD_Y_SHIFT;
  result |= rack_x;
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= chip_y;
  result <<= NOC_ADDR_NODE_ID_BITS;
  result |= chip_x; 
  return result;
}

inline uint32_t broadcast_tag_get_rack(uint32_t broadcast_tag) {
  return (broadcast_tag >> 2*NOC_ADDR_NODE_ID_BITS) & BROADCAST_TAG_RACK_MASK;
}

inline uint64_t get_sys_rack(uint32_t rack_x, uint32_t rack_y) {
  uint32_t result = rack_y;
  result <<= CHIP_COORD_Y_SHIFT;
  result |= rack_x;

  return result;
}

inline uint32_t get_rack_y(uint32_t rack) {
  return (rack >> (CHIP_COORD_Y_SHIFT)) & CHIP_COORD_MASK;
}

inline uint32_t get_rack_x(uint32_t rack) {
  return (rack >> (CHIP_COORD_X_SHIFT)) & CHIP_COORD_MASK;
}

inline uint32_t sys_addr_get_chip_y(uint64_t sys_addr) {
  return (sys_addr >> (NOC_ADDR_LOCAL_BITS+3*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}

inline uint32_t sys_addr_tag_get_chip_y(uint32_t sys_addr_tag) {
  return (sys_addr_tag >> (3*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}

inline uint32_t broadcast_tag_get_chip_y(uint32_t broadcast_tag) {
  return (broadcast_tag >> NOC_ADDR_NODE_ID_BITS) & NOC_NODE_ID_MASK;
}

inline uint32_t sys_addr_get_chip_x(uint64_t sys_addr) {
  return (sys_addr >> (NOC_ADDR_LOCAL_BITS+2*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}

inline uint32_t sys_addr_tag_get_chip_x(uint32_t sys_addr_tag) {
  return (sys_addr_tag >> (2*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}

inline uint32_t broadcast_tag_get_chip_x(uint32_t broadcast_tag) {
  return (broadcast_tag & NOC_NODE_ID_MASK);
}

inline uint32_t sys_addr_get_noc_y(uint64_t sys_addr) {
  return (sys_addr >> (NOC_ADDR_LOCAL_BITS+1*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}

inline uint32_t sys_addr_tag_get_noc_y(uint32_t sys_addr_tag) {
  return (sys_addr_tag >> (1*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}

inline uint32_t sys_addr_get_noc_x(uint64_t sys_addr) {
  return (sys_addr >> NOC_ADDR_LOCAL_BITS) & NOC_NODE_ID_MASK;
}

inline uint32_t sys_addr_tag_get_noc_x(uint32_t sys_addr_tag) {
  return (sys_addr_tag & NOC_NODE_ID_MASK);
}

inline uint64_t sys_addr_get_offset(uint64_t sys_addr) {
  const uint64_t noc_addr_local_bits_mask = ((((uint64_t)0x1) << NOC_ADDR_LOCAL_BITS) - 1);
  return (sys_addr & noc_addr_local_bits_mask);
}

inline uint32_t noc_xy_get_dest_index(uint32_t dest_x, uint32_t dest_y) {
  return dest_y*NOC_SIZE_X + dest_x;
}

inline uint32_t sys_addr_get_dest_index(uint64_t sys_addr) {
  uint32_t dest_x = sys_addr_get_noc_x(sys_addr);
  uint32_t dest_y = sys_addr_get_noc_y(sys_addr);
  return noc_xy_get_dest_index(dest_x, dest_y);
}

inline uint32_t noc_xy_get_erisc_index(uint32_t dest_x, uint32_t dest_y) {
    if (dest_x < 5) {
      return (dest_x * 2 - 1) + (dest_y == 0 ? 0 : 8);
    } else {
      return (9 - dest_x) * 2 + (dest_y == 0 ? 0 : 8);
    }
  }

inline uint32_t sys_addr_get_erisc_dest_index(uint64_t sys_addr) {
  uint32_t dest_x = sys_addr_get_noc_x(sys_addr);
  uint32_t dest_y = sys_addr_get_noc_y(sys_addr);
  return noc_xy_get_erisc_index(dest_x, dest_y);
}

inline uint32_t sys_addr_tag_get_erisc_dest_index(uint32_t sys_addr_tag) {
  uint32_t dest_x = sys_addr_tag_get_noc_x(sys_addr_tag);
  uint32_t dest_y = sys_addr_tag_get_noc_y(sys_addr_tag);
  return noc_xy_get_erisc_index(dest_x, dest_y);
}

inline void remote_update_ptr_advance(remote_update_ptr_t* rptr) {
  rptr->ptr = (rptr->ptr + 1) & CMD_BUF_PTR_MASK;
}

inline void cmd_q_advance_wrptr(cmd_q_t* cmd_q) {
  remote_update_ptr_advance(&(cmd_q->wrptr));
}

inline void cmd_q_advance_rdptr(cmd_q_t* cmd_q) {
  remote_update_ptr_advance(&(cmd_q->rdptr));
}

inline bool cmd_q_ptrs_empty(uint32_t wrptr, uint32_t rdptr) {
  return (wrptr == rdptr);
}

inline bool cmd_q_ptrs_full(uint32_t wrptr, uint32_t rdptr) {
  return !cmd_q_ptrs_empty(wrptr, rdptr) && ((wrptr & CMD_BUF_SIZE_MASK) == (rdptr & CMD_BUF_SIZE_MASK));
}

inline bool cmd_q_is_empty(const cmd_q_t* cmd_q) {
  return cmd_q_ptrs_empty(cmd_q->wrptr.ptr, cmd_q->rdptr.ptr);
}

inline bool cmd_q_is_full(const cmd_q_t* cmd_q) {
  return cmd_q_ptrs_full(cmd_q->wrptr.ptr, cmd_q->rdptr.ptr);
}

inline void node_remote_update_ptr_advance(remote_update_ptr_t* rptr) {
  rptr->ptr = (rptr->ptr + 1) & NODE_CMD_BUF_PTR_MASK;
}

inline void node_cmd_q_advance_wrptr(node_cmd_q_t* cmd_q) {
  node_remote_update_ptr_advance(&(cmd_q->wrptr));
}

inline void node_cmd_q_advance_rdptr(node_cmd_q_t* cmd_q) {
  //clear valid before incrementing read pointer.
  uint32_t rd_index = cmd_q->rdptr.ptr & NODE_CMD_BUF_SIZE_MASK;
  cmd_q->cmd[rd_index].Valid = 0;
  node_remote_update_ptr_advance(&(cmd_q->rdptr));
}

inline void node_advance_rdptr(uint32_t &rdptr) {
  rdptr = (rdptr + 1) & NODE_CMD_BUF_PTR_MASK;
}

inline bool node_cmd_q_ptrs_empty(uint32_t wrptr, uint32_t rdptr) {
  return (wrptr == rdptr);
}

inline bool node_cmd_q_ptrs_full(uint32_t wrptr, uint32_t rdptr) {
  //return !node_cmd_q_ptrs_empty(wrptr, rdptr) && ((wrptr & NODE_CMD_BUF_SIZE_MASK) == (rdptr & NODE_CMD_BUF_SIZE_MASK));
  uint32_t distance = wrptr >= rdptr ? wrptr - rdptr : wrptr + 2 * NODE_CMD_BUF_SIZE - rdptr;
  return !node_cmd_q_ptrs_empty(wrptr, rdptr) && (distance >= NODE_CMD_BUF_SIZE);
}

inline uint32_t node_cmd_q_ptrs_distance(uint32_t wrptr, uint32_t rdptr) {
  uint32_t distance = wrptr >= rdptr ? wrptr - rdptr : wrptr + 2 * NODE_CMD_BUF_SIZE - rdptr;
  return distance;
}

inline bool node_cmd_q_ptrs_full_with_size(uint32_t wrptr, uint32_t rdptr, uint32_t size) {
  uint32_t distance = wrptr >= rdptr ? wrptr - rdptr : wrptr + 2 * NODE_CMD_BUF_SIZE - rdptr;
  return !node_cmd_q_ptrs_empty(wrptr, rdptr) && (distance >= size);
}

inline bool node_cmd_q_is_empty(const node_cmd_q_t* cmd_q) {
  return cmd_q_ptrs_empty(cmd_q->wrptr.ptr, cmd_q->rdptr.ptr);
}

inline bool node_cmd_q_is_full(const node_cmd_q_t* cmd_q) {
  return node_cmd_q_ptrs_full(cmd_q->wrptr.ptr, cmd_q->rdptr.ptr);
}

inline bool node_cmd_valid(const node_cmd_q_t* cmd_q) {
  uint32_t rd_index = cmd_q->rdptr.ptr & NODE_CMD_BUF_SIZE_MASK;
  return cmd_q->cmd[rd_index].Valid == 1;
}

// Max 256B
const uint32_t NODE_INFO_MAX_SIZE_BYTES = 256;
typedef struct {
  uint32_t delimiter;
  uint32_t train_status;

  uint8_t my_rack_x;
  uint8_t my_rack_y;
  uint8_t my_chip_x;
  uint8_t my_chip_y;
  uint8_t root_node;
  uint8_t eth_node;
  uint8_t my_eth_id;
  uint8_t routing_node;

  uint32_t my_noc_x[NUM_NOCS];
  uint32_t my_noc_y[NUM_NOCS];
  uint64_t remote_eth_sys_addr;
  uint32_t remote_eth_rack;

  uint8_t  broadcast_state;
  uint8_t  eth_broadcast_state;
  uint16_t eth_port_full_duplex;
  uint32_t dram_bytes_processed;
  uint32_t dram_bytes_processed_resp;
  uint32_t rack_l_sys_addr_tag;
  uint32_t rack_r_sys_addr_tag;
  uint32_t rack_u_sys_addr_tag;
  uint32_t rack_d_sys_addr_tag;
  uint32_t req_pending[eth_q_id_t::NUM_Q];
  uint32_t wr_ptr[eth_q_id_t::NUM_Q];
  uint32_t my_sys_addr_tag;

  uint32_t broadcast_tags[4];
  uint8_t eth_broadcast_skip[4];
  uint32_t eth_port_txn_count[eth_route_direction_t::NUM+2];
  int8_t eth_port_min[eth_route_direction_t::COUNT];
  int8_t eth_port_max[eth_route_direction_t::COUNT];
  int8_t eth_port_active[eth_route_direction_t::COUNT];
  int8_t eth_port_static[eth_route_direction_t::COUNT];

  uint64_t timestamp;

  uint8_t timestamp_active;
  uint8_t timestamp_index;
  uint8_t host_mem_txn_id;
  uint8_t host_backoff;

  uint8_t eth_port_q_size[NUM_ETH_INST];
  uint32_t backoff_counter;
  uint8_t nebula_to_galxy;
  uint8_t last_request_pending_on_noc;
  uint8_t txn_id_flushed[2];
  uint32_t noc_fwd_counter;
} node_info_t; 

static_assert(sizeof(node_info_t) <= NODE_INFO_MAX_SIZE_BYTES);
static_assert(sizeof(node_info_t) == 54*4);

typedef struct {
  eth_conn_info_t eth_conn_info[NUM_ETH_INST];
  uint8_t shelf_router_chips[MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2];
  uint8_t rack_to_rack_router_chips[MAX_RACK_SHELVES * MAX_BOARD_CONNECTIONS_X * 2]; // one word for each chip that can be connected to next rack.
  uint32_t padding[6];
} system_connection_t;

inline bool sys_addr_is_local_rack(uint32_t rack, const node_info_t* local_node_info) {
  return (get_rack_x(rack) == local_node_info->my_rack_x) &&
         (get_rack_y(rack) == local_node_info->my_rack_y);
}

inline bool sys_addr_is_local_chip(uint32_t rack, uint64_t sys_addr, const node_info_t* local_node_info) {
  return sys_addr_is_local_rack(rack, local_node_info) && 
         (sys_addr_get_chip_x(sys_addr) == local_node_info->my_chip_x) &&
         (sys_addr_get_chip_y(sys_addr) == local_node_info->my_chip_y);
}

inline bool sys_addr_is_local(uint32_t noc_id, uint32_t rack, uint64_t sys_addr, const node_info_t* local_node_info) {
  return sys_addr_is_local_chip(rack, sys_addr, local_node_info) &&
         (sys_addr_get_noc_x(sys_addr) == local_node_info->my_noc_x[noc_id]) &&
         (sys_addr_get_noc_y(sys_addr) == local_node_info->my_noc_y[noc_id]);
}
/*
inline bool sys_addr_tag_is_local_chip(uint32_t rack, uint32_t sys_addr_tag, const node_info_t* local_node_info) {
  return sys_addr_is_local_rack(rack, local_node_info) && 
         (sys_addr_tag_get_chip_x(sys_addr_tag) == local_node_info->my_chip_x) &&
         (sys_addr_tag_get_chip_y(sys_addr_tag) == local_node_info->my_chip_y);
}

inline bool sys_addr_tag_is_local(uint32_t noc_id, uint32_t rack, uint32_t sys_addr_tag, const node_info_t* local_node_info) {
  return sys_addr_tag_is_local_chip(rack, sys_addr_tag, local_node_info) &&
         (sys_addr_tag_get_noc_x(sys_addr_tag) == local_node_info->my_noc_x[noc_id]) &&
         (sys_addr_tag_get_noc_y(sys_addr_tag) == local_node_info->my_noc_y[noc_id]);
}
*/
inline bool cmd_error(routing_cmd_t* resp_cmd) {
  return (resp_cmd->flags & CMD_DEST_UNREACHABLE) != 0;
}

inline bool node_cmd_error(node_cmd_t* resp_cmd) {
  return (resp_cmd->flags & CMD_DEST_UNREACHABLE) != 0;
}

uint32_t get_chip_x_from_router_index(uint32_t i);

uint32_t get_chip_y_from_router_index(uint32_t i);

int32_t get_y_router_chip_index(uint32_t chip_x, uint32_t chip_y);

int32_t get_x_router_chip_index(uint32_t chip_x, uint32_t chip_y);

uint32_t shelf_router_to_rack_router_chip_index(uint32_t i);

void noc_write_aligned_dw(uint32_t dest_noc_x, uint32_t dest_noc_y, uint32_t addr, uint32_t val);

void eth_routing_init();

void host_req_handler();

void eth_req_handler();

void eth_routing_handler();

void eth_link_status_check(void);

void cmd_service_noc(node_cmd_t* req_cmd, node_cmd_t* resp_cmd);

void noc_write (uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t size, bool no_flush);

void noc_read(uint64_t src_addr, uint32_t dest_addr, uint32_t size);
#endif

