# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

####

MEM_WORD_BYTES = 16
MEM_WORD_BIT_OFFSET_WIDTH = 7
MEM_WORD_ADDR_WIDTH = 17
NOC_ID_WIDTH = 6
NOC_ADDR_LOCAL_WIDTH = 36
STREAM_ID_WIDTH = 6
NOC_NUM_WIDTH = 1
NOC_NUM_STREAMS = 64
STREAM_REG_INDEX_WIDTH = 8
STREAM_REG_CFG_DATA_WIDTH = 24
MAX_TILES_MSG_INFO_BUF_PER_PHASE = 2048

####

STREAM_REMOTE_SRC_REG_INDEX           = 0
STREAM_REMOTE_SRC_X                   = 0
STREAM_REMOTE_SRC_X_WIDTH             = NOC_ID_WIDTH
STREAM_REMOTE_SRC_Y                   = (STREAM_REMOTE_SRC_X+STREAM_REMOTE_SRC_X_WIDTH)
STREAM_REMOTE_SRC_Y_WIDTH             = NOC_ID_WIDTH
REMOTE_SRC_STREAM_ID                  = (STREAM_REMOTE_SRC_Y+STREAM_REMOTE_SRC_Y_WIDTH)
REMOTE_SRC_STREAM_ID_WIDTH            = STREAM_ID_WIDTH
STREAM_REMOTE_SRC_DEST_INDEX          = (REMOTE_SRC_STREAM_ID+REMOTE_SRC_STREAM_ID_WIDTH)
STREAM_REMOTE_SRC_DEST_INDEX_WIDTH    = STREAM_ID_WIDTH
DRAM_READS__TRANS_SIZE_WORDS_LO       = (STREAM_REMOTE_SRC_Y+STREAM_REMOTE_SRC_Y_WIDTH)
DRAM_READS__TRANS_SIZE_WORDS_LO_WIDTH =  12
  
STREAM_REMOTE_SRC_PHASE_REG_INDEX = (STREAM_REMOTE_SRC_REG_INDEX+1)
  
STREAM_REMOTE_DEST_REG_INDEX  = (STREAM_REMOTE_SRC_PHASE_REG_INDEX+1)
STREAM_REMOTE_DEST_X                  = 0
STREAM_REMOTE_DEST_X_WIDTH            = NOC_ID_WIDTH
STREAM_REMOTE_DEST_Y                  = (STREAM_REMOTE_DEST_X+STREAM_REMOTE_DEST_X_WIDTH)
STREAM_REMOTE_DEST_Y_WIDTH            = NOC_ID_WIDTH
STREAM_REMOTE_DEST_STREAM_ID          = (STREAM_REMOTE_DEST_Y+STREAM_REMOTE_DEST_Y_WIDTH)
STREAM_REMOTE_DEST_STREAM_ID_WIDTH    = STREAM_ID_WIDTH
  
STREAM_LOCAL_DEST_REG_INDEX  = (STREAM_REMOTE_SRC_PHASE_REG_INDEX+1)
STREAM_LOCAL_DEST_MSG_CLEAR_NUM        =  0
STREAM_LOCAL_DEST_MSG_CLEAR_NUM_WIDTH  =  12
STREAM_LOCAL_DEST_STREAM_ID        =  (STREAM_LOCAL_DEST_MSG_CLEAR_NUM+STREAM_LOCAL_DEST_MSG_CLEAR_NUM_WIDTH)
STREAM_LOCAL_DEST_STREAM_ID_WIDTH  =  STREAM_ID_WIDTH
  
STREAM_REMOTE_DEST_BUF_START_REG_INDEX = (STREAM_REMOTE_DEST_REG_INDEX+1)
  
STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX  = (STREAM_REMOTE_DEST_BUF_START_REG_INDEX+1)
REMOTE_DEST_BUF_SIZE_WORDS        =   0
REMOTE_DEST_BUF_SIZE_WORDS_WIDTH  =   MEM_WORD_ADDR_WIDTH
 
STREAM_REMOTE_DEST_WR_PTR_REG_INDEX = (STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX+1)
  
STREAM_BUF_START_REG_INDEX  = (STREAM_REMOTE_DEST_WR_PTR_REG_INDEX+1)
  
STREAM_BUF_SIZE_REG_INDEX  = (STREAM_BUF_START_REG_INDEX+1)
  
STREAM_MSG_INFO_PTR_REG_INDEX = (STREAM_BUF_SIZE_REG_INDEX+1)
  
STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX = (STREAM_MSG_INFO_PTR_REG_INDEX+1)
  
STREAM_MISC_CFG_REG_INDEX  = (STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX+1)
INCOMING_DATA_NOC              = 0
INCOMING_DATA_NOC_WIDTH        = NOC_NUM_WIDTH
OUTGOING_DATA_NOC              = (INCOMING_DATA_NOC+INCOMING_DATA_NOC_WIDTH)
OUTGOING_DATA_NOC_WIDTH        = NOC_NUM_WIDTH
REMOTE_SRC_UPDATE_NOC          = (OUTGOING_DATA_NOC+OUTGOING_DATA_NOC_WIDTH)
REMOTE_SRC_UPDATE_NOC_WIDTH    = NOC_NUM_WIDTH
LOCAL_SOURCES_CONNECTED        = (REMOTE_SRC_UPDATE_NOC+REMOTE_SRC_UPDATE_NOC_WIDTH)
LOCAL_SOURCES_CONNECTED_WIDTH  = 1
SOURCE_ENDPOINT                = (LOCAL_SOURCES_CONNECTED+LOCAL_SOURCES_CONNECTED_WIDTH)
SOURCE_ENDPOINT_WIDTH          = 1
REMOTE_SOURCE                  = (SOURCE_ENDPOINT+SOURCE_ENDPOINT_WIDTH)
REMOTE_SOURCE_WIDTH            = 1
RECEIVER_ENDPOINT              = (REMOTE_SOURCE+REMOTE_SOURCE_WIDTH)
RECEIVER_ENDPOINT_WIDTH        = 1
LOCAL_RECEIVER                 = (RECEIVER_ENDPOINT+RECEIVER_ENDPOINT_WIDTH)
LOCAL_RECEIVER_WIDTH           = 1
REMOTE_RECEIVER                = (LOCAL_RECEIVER+LOCAL_RECEIVER_WIDTH)
REMOTE_RECEIVER_WIDTH          = 1
PHASE_AUTO_CONFIG              = (REMOTE_RECEIVER+REMOTE_RECEIVER_WIDTH)
PHASE_AUTO_CONFIG_WIDTH        = 1
PHASE_AUTO_ADVANCE             = (PHASE_AUTO_CONFIG+PHASE_AUTO_CONFIG_WIDTH)
PHASE_AUTO_ADVANCE_WIDTH       = 1
DATA_AUTO_SEND                 = (PHASE_AUTO_ADVANCE+PHASE_AUTO_ADVANCE_WIDTH)
DATA_AUTO_SEND_WIDTH           = 1
NEXT_PHASE_SRC_CHANGE          =(DATA_AUTO_SEND+DATA_AUTO_SEND_WIDTH)
NEXT_PHASE_SRC_CHANGE_WIDTH    = 1
NEXT_PHASE_DEST_CHANGE         =(NEXT_PHASE_SRC_CHANGE+NEXT_PHASE_SRC_CHANGE_WIDTH)
NEXT_PHASE_DEST_CHANGE_WIDTH   = 1
DATA_BUF_NO_FLOW_CTRL          = (NEXT_PHASE_DEST_CHANGE+NEXT_PHASE_DEST_CHANGE_WIDTH)
DATA_BUF_NO_FLOW_CTRL_WIDTH    = 1
DEST_DATA_BUF_NO_FLOW_CTRL     = (DATA_BUF_NO_FLOW_CTRL+DATA_BUF_NO_FLOW_CTRL_WIDTH)
DEST_DATA_BUF_NO_FLOW_CTRL_WIDTH = 1
REMOTE_SRC_IS_MCAST           =  (DEST_DATA_BUF_NO_FLOW_CTRL+DEST_DATA_BUF_NO_FLOW_CTRL_WIDTH)
REMOTE_SRC_IS_MCAST_WIDTH     =  1
NO_PREV_PHASE_OUTGOING_DATA_FLUSH = (REMOTE_SRC_IS_MCAST+REMOTE_SRC_IS_MCAST_WIDTH)
NO_PREV_PHASE_OUTGOING_DATA_FLUSH_WIDTH   =    1
UNICAST_VC_REG                  = (NO_PREV_PHASE_OUTGOING_DATA_FLUSH+NO_PREV_PHASE_OUTGOING_DATA_FLUSH_WIDTH)
UNICAST_VC_REG_WIDTH            =  3
REG_UPDATE_VC_REG               = (UNICAST_VC_REG+UNICAST_VC_REG_WIDTH)
REG_UPDATE_VC_REG_WIDTH         =  3
  
STREAM_CURR_PHASE_REG_INDEX = (STREAM_MISC_CFG_REG_INDEX+1)
  
STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX = (STREAM_CURR_PHASE_REG_INDEX+1)
  
STREAM_MCAST_DEST_REG_INDEX = (STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX+1)
STREAM_MCAST_END_X               = 0
STREAM_MCAST_END_X_WIDTH         = NOC_ID_WIDTH
STREAM_MCAST_END_Y               = (STREAM_MCAST_END_X+STREAM_MCAST_END_X_WIDTH)
STREAM_MCAST_END_Y_WIDTH         = NOC_ID_WIDTH
STREAM_MCAST_EN                  = (STREAM_MCAST_END_Y+STREAM_MCAST_END_Y_WIDTH)
STREAM_MCAST_EN_WIDTH            = 1
STREAM_MCAST_LINKED              = (STREAM_MCAST_EN+STREAM_MCAST_EN_WIDTH)
STREAM_MCAST_LINKED_WIDTH        = 1
STREAM_MCAST_VC                  = (STREAM_MCAST_LINKED+STREAM_MCAST_LINKED_WIDTH)
STREAM_MCAST_VC_WIDTH            = 1
STREAM_MCAST_NO_PATH_RES         = (STREAM_MCAST_VC+STREAM_MCAST_VC_WIDTH)
STREAM_MCAST_NO_PATH_RES_WIDTH   = 1
STREAM_MCAST_XY                  = (STREAM_MCAST_NO_PATH_RES+STREAM_MCAST_NO_PATH_RES_WIDTH)
STREAM_MCAST_XY_WIDTH            = 1
STREAM_MCAST_SRC_SIDE_DYNAMIC_LINKED         = (STREAM_MCAST_XY+STREAM_MCAST_XY_WIDTH)
STREAM_MCAST_SRC_SIDE_DYNAMIC_LINKED_WIDTH   = 1
STREAM_MCAST_DEST_SIDE_DYNAMIC_LINKED        = (STREAM_MCAST_SRC_SIDE_DYNAMIC_LINKED+STREAM_MCAST_SRC_SIDE_DYNAMIC_LINKED_WIDTH)
STREAM_MCAST_DEST_SIDE_DYNAMIC_LINKED_WIDTH  = 1
  
STREAM_MCAST_DEST_NUM_REG_INDEX = (STREAM_MCAST_DEST_REG_INDEX+1)
  
STREAM_GATHER_REG_INDEX = (STREAM_MCAST_DEST_NUM_REG_INDEX+1)
MSG_ARB_GROUP_SIZE             =   0
MSG_ARB_GROUP_SIZE_WIDTH       =   3
MSG_SRC_IN_ORDER_FWD           =   (MSG_ARB_GROUP_SIZE+MSG_ARB_GROUP_SIZE_WIDTH)
MSG_SRC_IN_ORDER_FWD_WIDTH     =   1
  
STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX = (STREAM_GATHER_REG_INDEX+1)
  
STREAM_MSG_HEADER_FORMAT_REG_INDEX = (STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX+1)
MSG_HEADER_WORD_CNT_OFFSET       = 0
MSG_HEADER_WORD_CNT_OFFSET_WIDTH = MEM_WORD_BIT_OFFSET_WIDTH
MSG_HEADER_WORD_CNT_BITS         = (MSG_HEADER_WORD_CNT_OFFSET+MSG_HEADER_WORD_CNT_OFFSET_WIDTH)
MSG_HEADER_WORD_CNT_BITS_WIDTH   = MEM_WORD_BIT_OFFSET_WIDTH
  
STREAM_NUM_MSGS_RECEIVED_REG_INDEX = (STREAM_MSG_HEADER_FORMAT_REG_INDEX+1)
  
STREAM_NEXT_RECEIVED_MSG_ADDR_REG_INDEX = (STREAM_NUM_MSGS_RECEIVED_REG_INDEX+1)
  
STREAM_NEXT_RECEIVED_MSG_SIZE_REG_INDEX = (STREAM_NEXT_RECEIVED_MSG_ADDR_REG_INDEX+1)
  
STREAM_MSG_INFO_CLEAR_REG_INDEX = (STREAM_NEXT_RECEIVED_MSG_SIZE_REG_INDEX+1)
  
STREAM_MSG_DATA_CLEAR_REG_INDEX = (STREAM_MSG_INFO_CLEAR_REG_INDEX+1)
  
STREAM_NEXT_MSG_SEND_REG_INDEX = (STREAM_MSG_DATA_CLEAR_REG_INDEX+1)
  
STREAM_RD_PTR_REG_INDEX = (STREAM_NEXT_MSG_SEND_REG_INDEX+1)
  
STREAM_WR_PTR_REG_INDEX = (STREAM_RD_PTR_REG_INDEX+1) 
  
STREAM_MSG_INFO_WR_PTR_REG_INDEX = (STREAM_WR_PTR_REG_INDEX+1) 
  
STREAM_PHASE_ADVANCE_REG_INDEX = (STREAM_MSG_INFO_WR_PTR_REG_INDEX+1)

STREAM_BUF_SPACE_AVAILABLE_REG_INDEX = (STREAM_PHASE_ADVANCE_REG_INDEX+1)
  
STREAM_SOURCE_ENDPOINT_NEW_MSG_INFO_REG_INDEX = (STREAM_BUF_SPACE_AVAILABLE_REG_INDEX+1)
SOURCE_ENDPOINT_NEW_MSG_ADDR          = 0
SOURCE_ENDPOINT_NEW_MSG_ADDR_WIDTH    = MEM_WORD_ADDR_WIDTH
SOURCE_ENDPOINT_NEW_MSG_SIZE          = (SOURCE_ENDPOINT_NEW_MSG_ADDR+SOURCE_ENDPOINT_NEW_MSG_ADDR_WIDTH)
SOURCE_ENDPOINT_NEW_MSG_SIZE_WIDTH    = (32-MEM_WORD_ADDR_WIDTH)
  
STREAM_NUM_MSGS_RECEIVED_INC_REG_INDEX = (STREAM_SOURCE_ENDPOINT_NEW_MSG_INFO_REG_INDEX+1)
SOURCE_ENDPOINT_NEW_MSGS_NUM                = 0
SOURCE_ENDPOINT_NEW_MSGS_NUM_WIDTH          = 12
SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE         = (SOURCE_ENDPOINT_NEW_MSGS_NUM+SOURCE_ENDPOINT_NEW_MSGS_NUM_WIDTH)
SOURCE_ENDPOINT_NEW_MSGS_TOTAL_SIZE_WIDTH   = MEM_WORD_ADDR_WIDTH
  
STREAM_RESET_REG_INDEX = (STREAM_NUM_MSGS_RECEIVED_INC_REG_INDEX+1)
  
STREAM_DEST_PHASE_READY_UPDATE_REG_INDEX = (STREAM_RESET_REG_INDEX+1)
PHASE_READY_DEST_NUM         = 0
PHASE_READY_DEST_NUM_WIDTH   = 6
PHASE_READY_NUM              = (PHASE_READY_DEST_NUM+PHASE_READY_DEST_NUM_WIDTH)
PHASE_READY_NUM_WIDTH        = 20
PHASE_READY_MCAST            = (PHASE_READY_NUM+PHASE_READY_NUM_WIDTH)
PHASE_READY_MCAST_WIDTH      = 1
PHASE_READY_TWO_WAY_RESP     = (PHASE_READY_MCAST+PHASE_READY_MCAST_WIDTH)
PHASE_READY_TWO_WAY_RESP_WIDTH = 1
  
STREAM_SRC_READY_UPDATE_REG_INDEX = (STREAM_DEST_PHASE_READY_UPDATE_REG_INDEX+1)
STREAM_REMOTE_RDY_SRC_X            =     0
STREAM_REMOTE_RDY_SRC_X_WIDTH      =     NOC_ID_WIDTH
STREAM_REMOTE_RDY_SRC_Y            =     (STREAM_REMOTE_RDY_SRC_X+STREAM_REMOTE_RDY_SRC_X_WIDTH)
STREAM_REMOTE_RDY_SRC_Y_WIDTH      =     NOC_ID_WIDTH
REMOTE_RDY_SRC_STREAM_ID           =     (STREAM_REMOTE_RDY_SRC_Y+STREAM_REMOTE_RDY_SRC_Y_WIDTH)
REMOTE_RDY_SRC_STREAM_ID_WIDTH     =     STREAM_ID_WIDTH
  
STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_REG_INDEX = (STREAM_SRC_READY_UPDATE_REG_INDEX+1)
REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM        = 0
REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM_WIDTH  = 6
REMOTE_DEST_BUF_WORDS_FREE_INC                         = (REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM+REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_DEST_NUM_WIDTH)
REMOTE_DEST_BUF_WORDS_FREE_INC_WIDTH                   = MEM_WORD_ADDR_WIDTH
  
STREAM_WAIT_STATUS_REG_INDEX = (STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_REG_INDEX+1)
WAIT_SW_PHASE_ADVANCE_SIGNAL       = 0
WAIT_SW_PHASE_ADVANCE_SIGNAL_WIDTH = 1
WAIT_PREV_PHASE_DATA_FLUSH         = (WAIT_SW_PHASE_ADVANCE_SIGNAL+WAIT_SW_PHASE_ADVANCE_SIGNAL_WIDTH)
WAIT_PREV_PHASE_DATA_FLUSH_WIDTH   = 1
MSG_FWD_ONGOING                    = (WAIT_PREV_PHASE_DATA_FLUSH+WAIT_PREV_PHASE_DATA_FLUSH_WIDTH)
MSG_FWD_ONGOING_WIDTH              = 1
STREAM_CURR_STATE                  = (MSG_FWD_ONGOING+MSG_FWD_ONGOING_WIDTH)
STREAM_CURR_STATE_WIDTH            = 4
  
STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX = (STREAM_WAIT_STATUS_REG_INDEX+1)
PHASE_NUM_INCR                         =  0
PHASE_NUM_INCR_WIDTH                   =  12 
CURR_PHASE_NUM_MSGS                    =  (PHASE_NUM_INCR+PHASE_NUM_INCR_WIDTH)
CURR_PHASE_NUM_MSGS_WIDTH              =  12
NEXT_PHASE_NUM_CFG_REG_WRITES          =  (CURR_PHASE_NUM_MSGS+CURR_PHASE_NUM_MSGS_WIDTH)
NEXT_PHASE_NUM_CFG_REG_WRITES_WIDTH    =  8
  
STREAM_PERF_CONFIG_REG_INDEX = (STREAM_PHASE_AUTO_CFG_HEADER_REG_INDEX+1)
CLOCK_GATING_EN              =  0
CLOCK_GATING_EN_WIDTH        =  1  
CLOCK_GATING_HYST            =  (CLOCK_GATING_EN+CLOCK_GATING_EN_WIDTH)
CLOCK_GATING_HYST_WIDTH      =  7  
PARTIAL_SEND_WORDS_THR       =  (CLOCK_GATING_HYST+CLOCK_GATING_HYST_WIDTH)
PARTIAL_SEND_WORDS_THR_WIDTH =  8
  
STREAM_MSG_GROUP_ZERO_MASK_AND_REG_INDEX = (STREAM_PERF_CONFIG_REG_INDEX+1)
  
STREAM_MSG_INFO_FULL_REG_INDEX = (STREAM_MSG_GROUP_ZERO_MASK_AND_REG_INDEX+1)
  
STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX = (STREAM_MSG_INFO_FULL_REG_INDEX+1)
  
STREAM_MSG_INFO_CAN_PUSH_NEW_MSG_REG_INDEX = (STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX+1)
  
STREAM_MSG_GROUP_COMPRESS_REG_INDEX = (STREAM_MSG_INFO_CAN_PUSH_NEW_MSG_REG_INDEX+1)
  
STREAM_GATHER_CLEAR_REG_INDEX = (STREAM_MSG_GROUP_COMPRESS_REG_INDEX+1)
MSG_LOCAL_STREAM_CLEAR_NUM        = 0
MSG_LOCAL_STREAM_CLEAR_NUM_WIDTH  = 16
MSG_GROUP_STREAM_CLEAR_TYPE       = (MSG_LOCAL_STREAM_CLEAR_NUM+MSG_LOCAL_STREAM_CLEAR_NUM_WIDTH)
MSG_GROUP_STREAM_CLEAR_TYPE_WIDTH = 1
  
STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX = (STREAM_GATHER_CLEAR_REG_INDEX+1)
  
STREAM_DEBUG_STATUS_SEL_REG_INDEX = (STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX+1)
DEBUG_STATUS_STREAM_ID_SEL        = 0
DEBUG_STATUS_STREAM_ID_SEL_WIDTH  = STREAM_ID_WIDTH
DISABLE_DEST_READY_TABLE          = (DEBUG_STATUS_STREAM_ID_SEL+DEBUG_STATUS_STREAM_ID_SEL_WIDTH)
DISABLE_DEST_READY_TABLE_WIDTH    = 1
  
STREAM_DEBUG_ASSERTIONS_REG_INDEX = (STREAM_DEBUG_STATUS_SEL_REG_INDEX+1)
  
STREAM_NUM_MSGS_RECEIVED_IN_BUF_AND_MEM_REG_INDEX = (STREAM_DEBUG_ASSERTIONS_REG_INDEX+1)
  
STREAM_LOCAL_SRC_MASK_REG_INDEX = 48
  
STREAM_RECEIVER_ENDPOINT_SET_MSG_HEADER_REG_INDEX = 60
  
STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_REG_INDEX = 64
REMOTE_DEST_WORDS_FREE        =   0
REMOTE_DEST_WORDS_FREE_WIDTH  =   MEM_WORD_ADDR_WIDTH
  
STREAM_RECEIVER_MSG_INFO_REG_INDEX = 128
  
STREAM_DEBUG_STATUS_REG_INDEX   =    224
  
STREAM_BLOB_AUTO_CFG_DONE_REG_INDEX         =     234
  
STREAM_REMOTE_DEST_BUF_START_HI_REG_INDEX     =   242
  
STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_HI_REG_INDEX = 243
  
STREAM_CURR_PHASE_BASE_REG_INDEX        =         244
  
STREAM_PHASE_AUTO_CFG_PTR_BASE_REG_INDEX      =   245
  
STREAM_BLOB_NEXT_AUTO_CFG_DONE_REG_INDEX  =   246
BLOB_NEXT_AUTO_CFG_DONE_STREAM_ID         =   0
BLOB_NEXT_AUTO_CFG_DONE_STREAM_ID_WIDTH   =     STREAM_ID_WIDTH
BLOB_NEXT_AUTO_CFG_DONE_VALID             =   16
BLOB_NEXT_AUTO_CFG_DONE_VALID_WIDTH       =     1
  
FIRMWARE_SCRATCH_REG_INDEX     =     247
  
STREAM_SCRATCH_REG_INDEX         =         248
STREAM_SCRATCH_0_REG_INDEX       =         248
STREAM_SCRATCH_1_REG_INDEX       =         (STREAM_SCRATCH_0_REG_INDEX+1)
STREAM_SCRATCH_2_REG_INDEX       =         (STREAM_SCRATCH_1_REG_INDEX+1)
STREAM_SCRATCH_3_REG_INDEX       =         (STREAM_SCRATCH_2_REG_INDEX+1)
STREAM_SCRATCH_4_REG_INDEX       =         (STREAM_SCRATCH_3_REG_INDEX+1)
STREAM_SCRATCH_5_REG_INDEX       =         (STREAM_SCRATCH_4_REG_INDEX+1)
  
####

