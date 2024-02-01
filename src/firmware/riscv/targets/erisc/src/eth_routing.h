// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _ETH_ROUTING_H_

#include <stdint.h>
#include "eth_interface.h"
#include "eth_init.h"

//shelf to shelf connections on a board.
#define MAX_BOARD_CONNECTIONS_Y 8
//rack to rack connection son a board.
#define MAX_BOARD_CONNECTIONS_X 4

#define MAX_EDGE_ROUTERS (MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2)

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


inline uint64_t get_sys_addr(uint32_t chip_x, uint32_t chip_y, uint32_t noc_x, uint32_t noc_y, uint32_t offset) {
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
/*
inline uint32_t sys_addr_tag_get_chip_y(uint32_t sys_addr_tag) {
  return (sys_addr_tag >> (3*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}
*/
inline uint32_t sys_addr_get_chip_x(uint64_t sys_addr) {
  return (sys_addr >> (NOC_ADDR_LOCAL_BITS+2*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}
/*
inline uint32_t sys_addr_tag_get_chip_x(uint32_t sys_addr_tag) {
  return (sys_addr_tag >> (2*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}
*/
inline uint32_t sys_addr_get_noc_y(uint64_t sys_addr) {
  return (sys_addr >> (NOC_ADDR_LOCAL_BITS+1*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}
/*
inline uint32_t sys_addr_tag_get_noc_y(uint32_t sys_addr_tag) {
  return (sys_addr_tag >> (1*NOC_ADDR_NODE_ID_BITS)) & NOC_NODE_ID_MASK;
}
*/
inline uint32_t sys_addr_get_noc_x(uint64_t sys_addr) {
  return (sys_addr >> NOC_ADDR_LOCAL_BITS) & NOC_NODE_ID_MASK;
}
/*
inline uint32_t sys_addr_tag_get_noc_x(uint32_t sys_addr_tag) {
  return (sys_addr_tag & NOC_NODE_ID_MASK);
}
*/
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
/*
inline uint32_t sys_addr_tag_get_erisc_dest_index(uint32_t sys_addr_tag) {
  uint32_t dest_x = sys_addr_tag_get_noc_x(sys_addr_tag);
  uint32_t dest_y = sys_addr_tag_get_noc_y(sys_addr_tag);
  return noc_xy_get_erisc_index(dest_x, dest_y);
}
*/
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
 ROOT = 0,
 TOP = 1,
 RIGHT = 2,
 BOTTOM = 3,
 LEFT = 4,
 NUM_Q = 5
} eth_q_id_t;

typedef enum {
 PHASE_0 = 0,
 PHASE_1 = 1,
 PHASE_2 = 2,
 PHASE_3 = 3,
 PHASE_4 = 4,
 PHASE_5 = 5
} routing_discovery_phase_t;

// Max 256B
const uint32_t NODE_INFO_MAX_SIZE_BYTES = 256;
typedef struct {
  uint32_t delimiter;
  uint32_t train_status;
  uint32_t my_rack_x;
  uint32_t my_rack_y;
  uint32_t my_chip_x;
  uint32_t my_chip_y;
  uint32_t my_noc_x[NUM_NOCS];
  uint32_t my_noc_y[NUM_NOCS];
  uint8_t root_node;
  uint8_t eth_node;
  uint8_t my_eth_id;
  uint8_t routing_node;
  eth_q_id_t eth_req_q_id;
  uint32_t src_dest_index;
  uint32_t src_rack[eth_q_id_t::NUM_Q];
  uint32_t src_rdptr_val[eth_q_id_t::NUM_Q];
  uint32_t remote_eth_rack;
  uint64_t remote_eth_sys_addr;
  uint64_t src_sys_addr[eth_q_id_t::NUM_Q];
  uint32_t dest_x_route_l_sys_addr;
  uint32_t dest_x_route_r_sys_addr;
  uint32_t dest_y_route_u_sys_addr;
  uint32_t dest_y_route_d_sys_addr;
  uint32_t rack_route_x_sys_addr;
  uint32_t rack_route_y_sys_addr;
  int32_t  data_block_source[CMD_BUF_SIZE];
  int32_t  prev_hop_buf_index[CMD_BUF_SIZE];
  int32_t  next_hop_buf_index[CMD_BUF_SIZE];
} node_info_t; 

static_assert(sizeof(node_info_t) <= NODE_INFO_MAX_SIZE_BYTES);
static_assert(sizeof(node_info_t) == 54*4);

typedef struct {
  eth_conn_info_t eth_conn_info[NUM_ETH_INST];
  uint32_t rack_router_chips[MAX_BOARD_CONNECTIONS_Y * 2 + MAX_BOARD_CONNECTIONS_X * 2];
  uint32_t router_chip_count;
  routing_discovery_phase_t routing_discovery_phase;
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

const uint32_t ETH_QUEUES_SIZE_BYTES = CMD_Q_SIZE_BYTES * 14 + 2 * (REMOTE_UPDATE_PTR_SIZE_BYTES * NUM_ETH_INST * 2);

typedef struct {
  cmd_q_t req_cmd_q[eth_q_id_t::NUM_Q];
  cmd_q_t resp_cmd_q[eth_q_id_t::NUM_Q];
  cmd_q_t eth_out_req_cmd_q;
  cmd_q_t eth_in_req_cmd_q;
  cmd_q_t eth_out_resp_cmd_q;
  cmd_q_t eth_in_resp_cmd_q;
  remote_update_ptr_t dest_wr_ptr[2][NUM_ETH_INST]; // need one array for host request and one for requests coming on ethernet link. NUM_ETH_INST can be reduced to 4
                                                     // since we have at most 4 ethernet links for routing in one ethernet core. 
  remote_update_ptr_t dest_rd_ptr[2][NUM_ETH_INST];

} eth_queues_t;

static_assert(sizeof(eth_queues_t) == ETH_QUEUES_SIZE_BYTES);
static_assert(sizeof(eth_queues_t) <= 4096);

void noc_write_aligned_dw(uint32_t dest_noc_x, uint32_t dest_noc_y, uint32_t addr, uint32_t val);

void eth_routing_init();

void eth_routing_handler();

void eth_link_status_check(void);

#endif

