// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdint.h>
#include "noc_overlay_parameters.h"

#define AUTO_CFG_HEADER(next_phase_num_cfg_reg_writes, curr_phase_num_msgs, phase_num_incr) \
  ((uint32_t)(((next_phase_num_cfg_reg_writes) << 24) | ((curr_phase_num_msgs) << 12) | (phase_num_incr)))

#define AUTO_CFG_REG(reg_index, reg_val) \
  ((uint32_t)(((reg_val) << 8) | (reg_index & ((1 << STREAM_REG_CFG_DATA_WIDTH)-1))))

#define AUTO_CFG_REG_INDEX(h) (((h) >> 0) & 0xFF)
#define AUTO_CFG_REG_VAL(h)   (((h) >> 8) & 0xFFFFFF)

#define PHASE_WIDTH 20

#define MSG_SIZE_OFFSET 0
#define MSG_SIZE_BITS   17

#define MAX_MEM_BUF_SIZE 0x200000

#define PHASE_START 0

////

#define STREAM_CFG(field, val) ((val) << (field))

#define STREAM_REMOTE_SRC(src_x, src_y, src_stream_id, dest_index) \
  (((src_x) << STREAM_REMOTE_SRC_X) | ((src_y) << STREAM_REMOTE_SRC_Y) | ((src_stream_id) << REMOTE_SRC_STREAM_ID) | ((dest_index) << STREAM_REMOTE_SRC_DEST_INDEX))

#define STREAM_REMOTE_DEST(dest_x, dest_y, dest_stream_id)   \
  (((dest_x) << STREAM_REMOTE_DEST_X) | ((dest_y) << STREAM_REMOTE_DEST_Y) | ((dest_stream_id) << STREAM_REMOTE_DEST_STREAM_ID))

#define STREAM_LOCAL_DEST(dest_stream_id, msg_clear_num)   \
  (((dest_stream_id) << STREAM_LOCAL_DEST_STREAM_ID) | ((msg_clear_num) << STREAM_LOCAL_DEST_MSG_CLEAR_NUM))

#define STREAM_MCAST_DEST(mcast_en, dest_end_x, dest_end_y, linked, vc, no_path_res, stream_mcast_xy) \
  (((mcast_en) << STREAM_MCAST_EN) |                                    \
   ((dest_end_x) << STREAM_MCAST_END_X) |                               \
   ((dest_end_y) << STREAM_MCAST_END_Y) |                               \
   ((linked) << STREAM_MCAST_LINKED) |                                  \
   ((no_path_res) << STREAM_MCAST_NO_PATH_RES) |                        \
   ((stream_mcast_xy) << STREAM_MCAST_XY) |                             \
   ((vc) << STREAM_MCAST_VC))

#define STREAM_GATHER(arb_group_size, src_in_order_fwd) \
  (((arb_group_size) << MSG_ARB_GROUP_SIZE) |                           \
   ((src_in_order_fwd) << MSG_SRC_IN_ORDER_FWD))

#define STREAM_MCAST_GATHER_CLEAR(local_stream_clear_num, msg_group_stream_clear_type) \
  (((local_stream_clear_num) << MSG_LOCAL_STREAM_CLEAR_NUM) |           \
   ((msg_group_stream_clear_type) << MSG_GROUP_STREAM_CLEAR_TYPE))

#define STREAM_PERF_CONFIG(clock_gating_en, clock_gating_hyst, partial_send_words_thr)	                \
  (((clock_gating_en & ((1 << CLOCK_GATING_EN_WIDTH)-1)) << CLOCK_GATING_EN) |                          \
   ((clock_gating_hyst & ((1 << CLOCK_GATING_HYST_WIDTH)-1)) << CLOCK_GATING_HYST) |                    \
   ((partial_send_words_thr & ((1 << PARTIAL_SEND_WORDS_THR_WIDTH)-1)) << PARTIAL_SEND_WORDS_THR))                 

#define STREAM_MSG_SIZE_FORMAT(offset, bits) \
  (((offset) << MSG_HEADER_WORD_CNT_OFFSET) | ((bits) << MSG_HEADER_WORD_CNT_BITS))

#define STREAM_REMOTE_BUF_SPACE(words, msgs)         \
  (((words) << REMOTE_DEST_BUF_SIZE_WORDS) | ((msgs) << REMOTE_DEST_BUF_SIZE_MSGS))

#define STREAM_SOURCE_ENDPOINT_NEW_MSG(addr, size) \
  (((addr) << SOURCE_ENDPOINT_NEW_MSG_ADDR) | ((size) << SOURCE_ENDPOINT_NEW_MSG_SIZE))

#define STREAM_SOURCE_ENDPOINT_MEM_BUF_NEW_MSGS_DATA(msgs, words) \
  (((msgs) <<  SOURCE_ENDPOINT_NEW_MSGS_NUM) | ((words) << SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE))

#define STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE(dest_num, words_free_inc, msgs_free_inc) \
  (((dest_num) << REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM) | ((words_free_inc) << REMOTE_DEST_BUF_WORDS_FREE_INC) | ((msgs_free_inc) << REMOTE_DEST_BUF_MSGS_FREE_INC))


#define NOC1_X_ID(id) (NOC_X_SIZE-1-(id))
#define NOC1_Y_ID(id) (NOC_Y_SIZE-1-(id))



#endif // NDEF OVERLAY_H

