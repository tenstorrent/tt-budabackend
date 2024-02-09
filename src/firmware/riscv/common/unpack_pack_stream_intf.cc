
#include "risc.h"
#include "unpack_pack_stream_intf.h"
#include "stream_interface.h"
#include "risc_common.h"
#include "risc_chip_specific.h"
#ifdef ERISC
#include "eth_init.h"
#include "eth_ss.h"
#endif
#ifdef PERF_DUMP
#include "brisc_perf.h"
#if OVERLAY_DECOUPLE == 1
extern bool first_timestamp_recorded_input[PERF_MAX_NUM_INPUTS];
extern bool last_timestamp_recorded_input[PERF_MAX_NUM_INPUTS];
#endif
#endif

volatile uint32_t tt_l1_ptr *trisc0_mailbox_ptr = (volatile uint32_t tt_l1_ptr *)(l1_mem::address_map::TRISC0_BASE + 0x4);
volatile uint32_t tt_l1_ptr *trisc1_mailbox_ptr = (volatile uint32_t tt_l1_ptr *)(l1_mem::address_map::TRISC1_BASE + 0x4);
volatile uint32_t tt_l1_ptr *trisc2_mailbox_ptr = (volatile uint32_t tt_l1_ptr *)(l1_mem::address_map::TRISC2_BASE + 0x4);

const uint32_t KERNEL_COMPLETE = 1;

////

#ifdef ERISC
extern uint32_t sender_tiles_received[32];
extern uint32_t sender_tiles_acked[32];
extern uint32_t receiver_tiles_received[32];
extern uint32_t receiver_tiles_acked[32];
#endif

inline uint32_t get_stream_id(kernel_output_stream_state_t* output_state) {
  return output_state->stream_id;
}

inline uint32_t get_active_idx(kernel_output_stream_state_t* output_state) {
  return output_state->active_streams_idx;
}

inline uint32_t get_fork_idx(kernel_output_stream_state_t* output_state, uint32_t fork_idx) {
  // Align properly to derefence fork_idxs
  fork_idx++;

  return output_state->fork_idxs[fork_idx];
}

inline bool is_scatter_loop_done(uint32_t &scatter_loop_done) {
  return ((scatter_loop_done >> 0) & 0x1);
}

inline bool is_fork_scatter_loop_done(uint32_t &scatter_loop_done, uint32_t fork_idx) {
  // Align properly to derefence fork_idxs
  fork_idx++;

  return ((scatter_loop_done >> fork_idx) & 0x1);
}

inline bool is_all_fork_scatter_loop_done(uint32_t &scatter_loop_done, uint32_t num_fork_streams) {
  uint32_t mask = (1 << (num_fork_streams+1)) - 1;
  return scatter_loop_done == mask;
}

inline void set_scatter_loop_done(uint32_t &scatter_loop_done) {
  scatter_loop_done |= (0x1 << 0);
}

inline void set_fork_scatter_loop_done(uint32_t &scatter_loop_done, uint32_t fork_idx) {
  // Align properly to derefence fork_idxs
  fork_idx++;

  scatter_loop_done |= (0x1 << fork_idx);
}

bool get_scatter_flushed(uint32_t stream_id, active_stream_info_t *active_stream_info, uint32_t active_streams_idx) {
  uint32_t dram_output_no_push = ((active_stream_info[active_streams_idx].flags & STREAM_DRAM_NO_PUSH) != 0);
  if (dram_output_no_push) {
    uint16_t operand_tiles_received = (uint16_t)*get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
    uint16_t operand_tiles_acked = (uint16_t)*get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
    uint16_t tiles_available = op_pack_tiles_ptr_sub(operand_tiles_received, operand_tiles_acked);
    return tiles_available == 0;
  } else {
    return stream_get_push_flushed(stream_id);
  }
}

void send_scatter_tiles(uint32_t stream_id, uint32_t num_tiles, uint32_t num_words, active_stream_info_t *active_stream_info, uint32_t active_streams_idx) {
  uint32_t dram_output_no_push = ((active_stream_info[active_streams_idx].flags & STREAM_DRAM_NO_PUSH) != 0);
  if (dram_output_no_push) {
    volatile uint32_t tt_reg_ptr* tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
    uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
    uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, num_tiles);
    tiles_received_ptr[0] = new_epoch_tiles_received;
  } else {
    stream_relay_tiles(stream_id, num_tiles, num_words);
  }
}

void run_pack_stream(kernel_output_stream_state_t* output_state, uint32_t stream_id, uint32_t active_streams_idx, uint32_t start_phase_num_cfg_regs) {
  volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx];
  uint32_t blob_size = l1_stream_info->blob_size;
  uint32_t blob_base_addr = l1_stream_info->blob_start_addr;
  uint32_t auto_cfg_header = stream_get_auto_cfg_header(stream_id);
  bool is_scatter = l1_stream_info->scatter_pipe_state != ((scatter_pipe_state_t*)0);
  uint32_t curr_unroll = 0;
  uint32_t scatter_idx = 0;
  uint32_t blob_start_addr = blob_base_addr;

  if (is_scatter) {
    scatter_idx = l1_stream_info->scatter_idx;
    curr_unroll = l1_stream_info->scatter_pipe_state[scatter_idx].curr_unroll;
    blob_start_addr = l1_stream_info->scatter_pipe_state[scatter_idx].unroll_blobs[curr_unroll].scatter_blob_base_addr;
    start_phase_num_cfg_regs = l1_stream_info->scatter_pipe_state[scatter_idx].unroll_blobs[curr_unroll].start_scatter_blob_num_cfg_regs;
  }

  uint32_t mblock_buffering_count = output_state->mblock_buffering_count;
  blob_start_addr += mulsi3(mblock_buffering_count,blob_size);

  if (auto_cfg_header == 0 || is_scatter) {
    stream_phase_blob_run(stream_id, blob_start_addr, start_phase_num_cfg_regs);
  } else {
    stream_phase_blob_run_offset(stream_id, blob_base_addr, blob_start_addr, blob_size);
  }

  if (is_scatter) {
    curr_unroll = curr_unroll + 1;
    uint32_t max_unroll = l1_stream_info->scatter_pipe_state[scatter_idx].max_unroll;
    if (curr_unroll >= max_unroll)
      curr_unroll = 0;
    l1_stream_info->scatter_pipe_state[scatter_idx].curr_unroll = curr_unroll;
  }
}

void process_pipe_scatter_output_loop(uint32_t active_streams_idx) {
  volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx];
  uint32_t scatter_order_size = l1_stream_info->scatter_order_size;
  if (scatter_order_size > 1) {
    uint32_t pipe_scatter_output_loop_size = l1_stream_info->pipe_scatter_output_loop_size;
    uint32_t pipe_scatter_output_loop_count = l1_stream_info->pipe_scatter_output_loop_count;
    pipe_scatter_output_loop_count++;
    if (pipe_scatter_output_loop_count >= pipe_scatter_output_loop_size) {
      uint32_t scatter_idx = l1_stream_info->scatter_idx;
      scatter_idx++;
      if (scatter_idx >= scatter_order_size)
        scatter_idx -= scatter_order_size;
      l1_stream_info->scatter_idx = scatter_idx;
      pipe_scatter_output_loop_count = 0;
    }
    l1_stream_info->pipe_scatter_output_loop_count = pipe_scatter_output_loop_count;
  }
}

void process_scatter_flush(uint32_t stream_id, active_stream_info_t *active_stream_info, uint32_t active_streams_idx, kernel_output_stream_state_t* prev_output_state,
                           volatile uint32_t tt_reg_ptr* epoch_tiles_acked_ptr, volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr, bool& all_kernels_done, uint32_t fork_idx,
                           uint32_t& scatter_inner_loop_done) {

  uint32_t num_msgs_in_block = fork_idx == ((uint32_t)-1) ? prev_output_state->num_msgs_in_block : get_fork_num_msgs_in_block(stream_id);
  uint32_t num_tiles_sending = (num_msgs_in_block & BRISC_PACKER_SENDING_IN_PROGRESS) != 0 ? num_msgs_in_block & ~BRISC_PACKER_SENDING_IN_PROGRESS : 0;

  if (num_tiles_sending) {
    bool push_flushed = get_scatter_flushed(stream_id, active_stream_info, active_streams_idx);
    if (push_flushed) {
      uint32_t scatter_inner_loop_count = fork_idx == ((uint32_t)-1) ? prev_output_state->scatter_inner_loop_count : get_fork_scatter_inner_loop_count(stream_id);

      scatter_inner_loop_count++;

      uint32_t num_scatter_inner_loop = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx]->num_scatter_inner_loop;
      if (scatter_inner_loop_count >= num_scatter_inner_loop) {
        if (fork_idx == ((uint32_t)-1))
          set_scatter_loop_done(scatter_inner_loop_done);
        else
          set_fork_scatter_loop_done(scatter_inner_loop_done, fork_idx);
      }

      num_msgs_in_block = num_tiles_sending;
      if (fork_idx == ((uint32_t)-1)) {
        prev_output_state->num_msgs_in_block = num_msgs_in_block;
        prev_output_state->scatter_inner_loop_count = scatter_inner_loop_count;
      } else {
        set_fork_num_msgs_in_block(stream_id, num_msgs_in_block);
        set_fork_scatter_inner_loop_count(stream_id, scatter_inner_loop_count);
      }
      prev_output_state->scatter_inner_loop_done = scatter_inner_loop_done;
    }

    all_kernels_done = false; // prevent breaking from outer loop
  }

}

void process_scatter_send(uint32_t stream_id, active_stream_info_t *active_stream_info, uint32_t active_streams_idx, kernel_output_stream_state_t* prev_output_state,
                          volatile uint32_t tt_reg_ptr* epoch_tiles_acked_ptr, volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr, bool& all_kernels_done, uint32_t fork_idx,
                          uint32_t& scatter_inner_loop_done) {

  uint32_t num_msgs_in_block = fork_idx == ((uint32_t)-1) ? prev_output_state->num_msgs_in_block : get_fork_num_msgs_in_block(stream_id);
  uint32_t num_tiles_sending = (num_msgs_in_block & BRISC_PACKER_SENDING_IN_PROGRESS) != 0 ? num_msgs_in_block & ~BRISC_PACKER_SENDING_IN_PROGRESS : 0;
  bool inner_loop_not_done = fork_idx == ((uint32_t)-1) ? !is_scatter_loop_done(scatter_inner_loop_done) : !is_fork_scatter_loop_done(scatter_inner_loop_done, fork_idx);

  if (inner_loop_not_done && !num_tiles_sending) {
    bool advance_wait = stream_phase_advance_wait(stream_id);
        
    uint32_t epoch_iters_remaining = active_stream_info[active_streams_idx].epoch_iters_remaining;
    if (advance_wait && epoch_iters_remaining > 0) {
      epoch_iters_remaining--;

      active_stream_info[active_streams_idx].epoch_iters_remaining = epoch_iters_remaining;
      uint32_t start_phase_num_cfg_regs = active_stream_info[active_streams_idx].start_phase_num_cfg_regs;
      run_pack_stream(prev_output_state, stream_id, active_streams_idx, start_phase_num_cfg_regs);
      if (epoch_iters_remaining == 0) {
        volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx];
        l1_stream_info->epoch_iters_remaining = 0;
      }
      all_kernels_done = false; // prevent breaking from outer loop
    }

    uint32_t phase_active = stream_phase_is_active(stream_id) && !is_dummy_phase(stream_id);
    
    uint16_t epoch_tiles_received = *epoch_tiles_received_ptr;
    uint16_t epoch_tiles_acked = *epoch_tiles_acked_ptr;
    uint16_t num_tiles_recv = op_pack_tiles_ptr_sub(epoch_tiles_received, epoch_tiles_acked);

    if (num_tiles_recv > 0 && num_tiles_recv >= num_msgs_in_block) {
      if (phase_active) {
        uint32_t num_tiles = stream_get_curr_phase_num_msgs(stream_id);
        uint32_t num_words = stream_get_data_buf_size(stream_id);
        if (prev_output_state->eth_fw_stream) {
          volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx];
          num_tiles = num_msgs_in_block == 0 ? num_tiles_recv : num_msgs_in_block;
          num_words = mulsi3(num_tiles, l1_stream_info->tile_size_words);
        }
        send_scatter_tiles(stream_id, num_tiles, num_words, active_stream_info, active_streams_idx);

        if (fork_idx == ((uint32_t)-1)) {
          prev_output_state->num_msgs_in_block = num_msgs_in_block | BRISC_PACKER_SENDING_IN_PROGRESS;
        } else {
          set_fork_num_msgs_in_block(stream_id, num_msgs_in_block | BRISC_PACKER_SENDING_IN_PROGRESS);
        }
      }

      all_kernels_done = false; // prevent breaking from outer loop
    } else {
#ifdef ERISC
      if (phase_active) {
        all_kernels_done = false;
      }
#endif
    }
  }

}

////
#ifdef ERISC
//Compiled On Erisc Core.
void curr_input_state_init(uint32_t num_input_streams, kernel_input_stream_state_t *input_stream_state) {
  kernel_input_stream_state_t* curr_input_state_ptr = (kernel_input_stream_state_t*)input_stream_state;
  kernel_input_stream_state_t* prev_input_state_ptr = curr_input_state_ptr;
  for (uint32_t input = 0; input < num_input_streams; input++) {
    volatile epoch_stream_info_t tt_l1_ptr * input_stream_info = RISC_EPOCH_INFO_PTR->inputs[input];
    curr_input_state_ptr->epoch_done = 0;
    curr_input_state_ptr->stream_id = input_stream_info->stream_id;
    curr_input_state_ptr->epoch_tiles_remaining_to_clear = input_stream_info->epoch_num_tiles;
    curr_input_state_ptr->num_iter_tiles = input_stream_info->num_iter_tiles;
    curr_input_state_ptr->no_hw_clearing = ((input_stream_info->flags & STREAM_INTERMEDIATE) != 0) || ((input_stream_info->flags & STREAM_MOVES_RAW_DATA) != 0) || ((input_stream_info->flags & STREAM_NCRISC_CLEAR) != 0) ||
                                           ((input_stream_info->flags & STREAM_DRAM_NO_PUSH) != 0);
    curr_input_state_ptr->epoch_tiles_cleared = 0;
    curr_input_state_ptr->prev_phases_tiles_received = 0;
    curr_input_state_ptr->tiles_to_clear = 0;

    uint32_t stream_type = input_stream_info->datacopy_stream_type & 0x7;
    curr_input_state_ptr->eth_fw_stream = 0;
    curr_input_state_ptr->msg_rd_addr = 0;
    curr_input_state_ptr->msg_wr_addr = 0;

    if (stream_type == datacopy_stream_type_t::PACKER) {
      curr_input_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_stream_tiles_received_ptr(input_stream_info->datacopy_stream_id);
      curr_input_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_stream_tiles_acked_ptr(curr_input_state_ptr->stream_id);
    } else if (stream_type & 0x4) {
      curr_input_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_l1_ptr *)&sender_tiles_received[input_stream_info->eth_remote_fw_stream_id];
      curr_input_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)&sender_tiles_acked[curr_input_state_ptr->stream_id];
      curr_input_state_ptr->eth_fw_stream = 1;
      curr_input_state_ptr->msg_wr_addr = stream_get_remote_data_buf_addr(curr_input_state_ptr->stream_id);
      curr_input_state_ptr->msg_rd_addr = stream_get_data_buf_addr(curr_input_state_ptr->stream_id);
    } else {
      curr_input_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_stream_tiles_received_ptr(curr_input_state_ptr->stream_id);
      curr_input_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_stream_tiles_acked_ptr(curr_input_state_ptr->stream_id);
    }

    curr_input_state_ptr->input_idx = input;
    curr_input_state_ptr->prev_ack_thresh = 0;
    curr_input_state_ptr->num_fork_streams = 0;


    curr_input_state_ptr->next = (curr_input_state_ptr + 1);
    prev_input_state_ptr = curr_input_state_ptr;
    curr_input_state_ptr++;
  }
  prev_input_state_ptr->next = input_stream_state;
}

//Compiled On Erisc Core.
void curr_output_state_init(uint32_t num_output_streams, kernel_output_stream_state_t *output_stream_state) {
  kernel_output_stream_state_t* curr_output_state_ptr = (kernel_output_stream_state_t*)output_stream_state;
  kernel_output_stream_state_t* prev_output_state_ptr = curr_output_state_ptr;
  for (uint32_t output = 0; output < num_output_streams; output++) {
    volatile epoch_stream_info_t tt_l1_ptr * output_stream_info = RISC_EPOCH_INFO_PTR->outputs[output];
    curr_output_state_ptr->epoch_done = 0;
    curr_output_state_ptr->stream_id = output_stream_info->stream_id;
    bool skip_processing = ((output_stream_info->flags & STREAM_INTERMEDIATE) != 0) || ((output_stream_info->flags & STREAM_MOVES_RAW_DATA) != 0) || output_stream_info->legacy_pack;
    bool fork_skip_processing = skip_processing;
    curr_output_state_ptr->skip_processing = skip_processing;
    curr_output_state_ptr->num_msgs_in_block = output_stream_info->num_msgs_in_block;

    uint32_t stream_type = output_stream_info->datacopy_stream_type & 0x7;
    curr_output_state_ptr->eth_fw_stream = 0;
    if (stream_type == datacopy_stream_type_t::UNPACKER) {
      curr_output_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_stream_tiles_received_ptr(curr_output_state_ptr->stream_id);
      curr_output_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_stream_tiles_acked_ptr(output_stream_info->datacopy_stream_id);
    } else if (stream_type & 0x4) {
      curr_output_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_l1_ptr*)&receiver_tiles_received[curr_output_state_ptr->stream_id];
      curr_output_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_l1_ptr*)&receiver_tiles_acked[output_stream_info->eth_remote_fw_stream_id];
      // curr_output_state_ptr->num_msgs_in_block = 1;
      curr_output_state_ptr->eth_fw_stream = 1;
    } else {
      curr_output_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_stream_tiles_received_ptr(curr_output_state_ptr->stream_id);
      curr_output_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_stream_tiles_acked_ptr(curr_output_state_ptr->stream_id);
    }

    curr_output_state_ptr->scatter_inner_loop_done = 0;
    curr_output_state_ptr->scatter_inner_loop_count = 0;
    curr_output_state_ptr->num_mblock_buffering = output_stream_info->num_mblock_buffering;
    curr_output_state_ptr->mblock_buffering_count = 0;
    for (uint32_t active_streams_idx = 0; active_streams_idx < NOC_NUM_STREAMS; active_streams_idx++) {
      if (curr_output_state_ptr->stream_id == RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx]->stream_id) {
        curr_output_state_ptr->active_streams_idx = active_streams_idx;
        break;
      }
    }

    uint32_t num_fork_streams = output_stream_info->num_fork_streams;
    curr_output_state_ptr->num_fork_streams = num_fork_streams;
    uint32_t total_fork_streams = (num_fork_streams+1);
    if (total_fork_streams > 0) {
      for (uint32_t k = 0; k < total_fork_streams; k++) {
        uint8_t fork_idx = output_stream_info->fork_idxs[k];
        curr_output_state_ptr->fork_idxs[k] = fork_idx;
        if (!fork_skip_processing && k >= 1) {
          uint32_t fork_stream_id = RISC_EPOCH_INFO_PTR->active_streams[fork_idx]->stream_id;
          set_fork_scatter_inner_loop_count(fork_stream_id, 0);
          set_fork_num_msgs_in_block(fork_stream_id, output_stream_info->num_msgs_in_block);
        }
      }
    }
    curr_output_state_ptr->output_idx = output;
    curr_output_state_ptr->next = (curr_output_state_ptr + 1);
    prev_output_state_ptr = curr_output_state_ptr;
    curr_output_state_ptr++;
  }
  prev_output_state_ptr->next = output_stream_state;
}

#else
//Compiled on Brisc Core.
void curr_input_state_init(uint32_t num_input_streams, kernel_input_stream_state_t *input_stream_state) {
  kernel_input_stream_state_t* curr_input_state_ptr = (kernel_input_stream_state_t*)input_stream_state;
  kernel_input_stream_state_t* prev_input_state_ptr = curr_input_state_ptr;
    for (uint32_t input = 0; input < num_input_streams; input++) {
    volatile epoch_stream_info_t tt_l1_ptr * input_stream_info = RISC_EPOCH_INFO_PTR->inputs[input];
    curr_input_state_ptr->epoch_done = 0;
    curr_input_state_ptr->stream_id = input_stream_info->stream_id;
    curr_input_state_ptr->epoch_tiles_remaining_to_clear = input_stream_info->epoch_num_tiles;
    curr_input_state_ptr->num_iter_tiles = input_stream_info->num_iter_tiles;
    curr_input_state_ptr->no_hw_clearing = ((input_stream_info->flags & STREAM_INTERMEDIATE) != 0) || ((input_stream_info->flags & STREAM_MOVES_RAW_DATA) != 0) || ((input_stream_info->flags & STREAM_NCRISC_CLEAR) != 0) ||
                                           ((input_stream_info->flags & STREAM_DRAM_NO_PUSH) != 0);
    curr_input_state_ptr->epoch_tiles_cleared = 0;
    curr_input_state_ptr->prev_phases_tiles_received = 0;
    curr_input_state_ptr->tiles_to_clear = 0;
    uint32_t operand = stream_id_to_operand(curr_input_state_ptr->stream_id);
    uint32_t stream_type = input_stream_info->datacopy_stream_type & 0x7;
    if (stream_type == datacopy_stream_type_t::PACKER) {
      curr_input_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_stream_tiles_received_ptr(input_stream_info->datacopy_stream_id);
      curr_input_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_stream_tiles_acked_ptr(curr_input_state_ptr->stream_id);
      curr_input_state_ptr->eth_fw_stream = 1;
    } else {
      curr_input_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_tiles_received_ptr(operand);
      curr_input_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_tiles_acked_ptr(operand);
      curr_input_state_ptr->eth_fw_stream = 0;
    }

    curr_input_state_ptr->input_idx = input;
    curr_input_state_ptr->prev_ack_thresh = 0;

    uint32_t num_fork_streams = input_stream_info->num_fork_streams;
    curr_input_state_ptr->num_fork_streams = num_fork_streams;
    if (num_fork_streams > 0) {
      for (uint32_t k = 0; k < num_fork_streams; k++) {
        uint8_t fork_stream_id = RISC_EPOCH_INFO_PTR->active_streams[input_stream_info->fork_idxs[k]]->stream_id;
        curr_input_state_ptr->fork_stream_ids[k] = fork_stream_id;
      }
      curr_input_state_ptr->fork_prev_pushed_num_tiles = 0;
    }


    curr_input_state_ptr->next = (curr_input_state_ptr + 1);
    prev_input_state_ptr = curr_input_state_ptr;
    curr_input_state_ptr++;
  }
  prev_input_state_ptr->next = input_stream_state;
}

//Compiled on Brisc Core.
void curr_output_state_init(uint32_t num_output_streams, kernel_output_stream_state_t *output_stream_state) {
  kernel_output_stream_state_t* curr_output_state_ptr = (kernel_output_stream_state_t*)output_stream_state;
  kernel_output_stream_state_t* prev_output_state_ptr = curr_output_state_ptr;
  for (uint32_t output = 0; output < num_output_streams; output++) {
    volatile epoch_stream_info_t tt_l1_ptr * output_stream_info = RISC_EPOCH_INFO_PTR->outputs[output];
    curr_output_state_ptr->epoch_done = 0;
    curr_output_state_ptr->stream_id = output_stream_info->stream_id;
    uint32_t operand = stream_id_to_operand(curr_output_state_ptr->stream_id);
    bool skip_processing = ((output_stream_info->flags & STREAM_INTERMEDIATE) != 0) || ((output_stream_info->flags & STREAM_MOVES_RAW_DATA) != 0);

    uint32_t stream_type = output_stream_info->datacopy_stream_type & 0x7;
    if (stream_type == datacopy_stream_type_t::UNPACKER) {
      curr_output_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_stream_tiles_received_ptr(curr_output_state_ptr->stream_id);
      curr_output_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_stream_tiles_acked_ptr(output_stream_info->datacopy_stream_id);
      curr_output_state_ptr->eth_fw_stream = output_stream_info->legacy_pack;
    } else {
      curr_output_state_ptr->epoch_tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_tiles_received_ptr(operand);
      curr_output_state_ptr->epoch_tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_packer_tiles_acked_ptr(operand);
      curr_output_state_ptr->eth_fw_stream = 0;
      skip_processing = skip_processing || output_stream_info->legacy_pack;
    }

    bool fork_skip_processing = skip_processing;
    curr_output_state_ptr->skip_processing = skip_processing;
    curr_output_state_ptr->num_msgs_in_block = output_stream_info->num_msgs_in_block;
    curr_output_state_ptr->scatter_inner_loop_done = 0;
    curr_output_state_ptr->scatter_inner_loop_count = 0;
    curr_output_state_ptr->num_mblock_buffering = output_stream_info->num_mblock_buffering;
    curr_output_state_ptr->mblock_buffering_count = 0;
    for (uint32_t active_streams_idx = 0; active_streams_idx < NOC_NUM_STREAMS; active_streams_idx++) {
      if (curr_output_state_ptr->stream_id == RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx]->stream_id) {
        curr_output_state_ptr->active_streams_idx = active_streams_idx;
        break;
      }
    }

    uint32_t num_fork_streams = output_stream_info->num_fork_streams;
    curr_output_state_ptr->num_fork_streams = num_fork_streams;
    uint32_t total_fork_streams = (num_fork_streams+1);
    if (total_fork_streams > 0) {
      for (uint32_t k = 0; k < total_fork_streams; k++) {
        uint8_t fork_idx = output_stream_info->fork_idxs[k];
        curr_output_state_ptr->fork_idxs[k] = fork_idx;
        if (!fork_skip_processing && k >= 1) {
          uint32_t fork_stream_id = RISC_EPOCH_INFO_PTR->active_streams[fork_idx]->stream_id;
          set_fork_scatter_inner_loop_count(fork_stream_id, 0);
          set_fork_num_msgs_in_block(fork_stream_id, output_stream_info->num_msgs_in_block);
        }
      }
    }
    curr_output_state_ptr->output_idx = output;

#if defined(PERF_DUMP) && (OVERLAY_DECOUPLE == 1)
    if (is_output_operand_decoupled(operand, output_decouple_mask)) {
      // Init output buffer with dummy tiles
      uint32_t stream_buf_size_bytes = output_stream_info->buf_full_size_bytes;
      uint32_t stream_buf_addr = output_stream_info->buf_base_addr;
      uint32_t stream_msg_info_buf_ptr = (output_stream_info->msg_info_buf_start)*MEM_WORD_WIDTH;
      uint32_t tile_size_words = *(volatile uint32_t tt_l1_ptr *)(stream_msg_info_buf_ptr);
      uint32_t tile_size_bytes = tile_size_words*MEM_WORD_WIDTH;
      for (uint32_t tile_header_ptr = stream_buf_addr; tile_header_ptr < stream_buf_addr + stream_buf_size_bytes; tile_header_ptr += tile_size_bytes) {
        *((uint32_t *)(tile_header_ptr)) = tile_size_words;
      }
    }
#endif

    curr_output_state_ptr->next = (curr_output_state_ptr + 1);
    prev_output_state_ptr = curr_output_state_ptr;
    curr_output_state_ptr++;
  }
  prev_output_state_ptr->next = output_stream_state;
}
#endif

void risc_unpack_pack_stream_handler_init(
  uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
  uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state
) {
  num_input_streams = RISC_EPOCH_INFO_PTR->num_inputs;
  num_output_streams = RISC_EPOCH_INFO_PTR->num_outputs;

  curr_input_state_init(num_input_streams, input_stream_state);
  curr_output_state_init(num_output_streams, output_stream_state);

  init_tile_clear();
}


void risc_stream_resart_check(
  uint32_t &stream_restart_check_cnt,
  uint32_t &num_active_streams, active_stream_info_t *active_stream_info
  ) {
  if (stream_done_hint())
    stream_restart_check_cnt = 0;

  bool check_stream_restart = (stream_restart_check_cnt == 0);
  stream_restart_check_cnt = (stream_restart_check_cnt + 1) & STREAM_RESTART_CHECK_MASK;

  if (check_stream_restart) {
    active_stream_info_t * curr_active_stream_info = active_stream_info;
    for (uint32_t i = 0;
        i < num_active_streams;
        i++, curr_active_stream_info++) {
          
      uint32_t stream_id = curr_active_stream_info->stream_id;
      uint32_t flags = curr_active_stream_info->flags;
      RISC_POST_STATUS(0xB0000000);

      check_dummy_phase(stream_id);

      if ((flags & STREAM_OUTPUT_PARK) != 0) {
        if ((flags & STREAM_INTERMEDIATE) != 0 || (flags & STREAM_MOVES_RAW_DATA) != 0) {
          volatile uint32_t tt_reg_ptr* tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
          volatile uint32_t tt_reg_ptr* tiles_acked_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_tiles_acked_ptr(stream_id_to_operand(stream_id));
          tiles_acked_ptr[0] = tiles_received_ptr[0];
        } else {
          // If there are already messages in the stream we must clear them before resetting the stream
          stream_clear_all_tiles(stream_id);
        }
      }

      // Reg read, flushed by immediate use
      uint32_t epoch_iters_remaining = curr_active_stream_info->epoch_iters_remaining;
      uint32_t stream_curr_iter_done = ((flags & STREAM_BRISC_PACK) == 0) && 
                                      ((flags & STREAM_MOVES_RAW_DATA) == 0) &&
                                      stream_phase_advance_wait(stream_id);
      if ((flags & STREAM_DRAM_INPUT) != 0 && ((flags & STREAM_DRAM_IO) != 0 || (flags & STREAM_DRAM_STREAMING) != 0)) {
        stream_curr_iter_done &= stream_src_endpoint_get_phase_tiles_count(stream_id) == 0;
      }

      if (stream_curr_iter_done) {
        if (epoch_iters_remaining == 0) {
          continue;
        }
        else {
          volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
          epoch_iters_remaining--;
          curr_active_stream_info->epoch_iters_remaining = epoch_iters_remaining;
          if (epoch_iters_remaining > 0) {
            stream_reset(stream_id);

            uint32_t blob_start_addr = l1_stream_info->blob_start_addr;
            uint32_t start_phase_num_cfg_regs = curr_active_stream_info->start_phase_num_cfg_regs;

            stream_phase_blob_run(stream_id, blob_start_addr, start_phase_num_cfg_regs);

            bool need_restart =
                ((flags & STREAM_DRAM_INPUT) != 0) && ((flags & STREAM_DRAM_IO) == 0) && ((flags & STREAM_DRAM_STREAMING) == 0) &&
                ((flags & STREAM_INTERMEDIATE) != 0 || (flags & STREAM_MOVES_RAW_DATA) != 0);
            curr_active_stream_info->dram_prefetch_stream_need_restart = need_restart;
            stream_restart_check_cnt = !need_restart;
          }
          else {
            l1_stream_info->epoch_iters_remaining = 0;
#if defined(PERF_DUMP) && (OVERLAY_DECOUPLE == 1)
            uint8_t operand_idx = uint8_t(stream_id_to_operand(stream_id));
            if (!perf::last_bw_timestamp_recorded(operand_idx, true)) {
              if (is_input_operand_decoupled(operand_idx)) {
                perf::record_input_end_bw_timestamp(operand_idx);
              }
            }
#endif
          }
        }
      }
      else if (curr_active_stream_info->dram_prefetch_stream_need_restart) {
        uint32_t stream_restarted = stream_phase_is_active(stream_id);
        if (stream_restarted) {                     
          uint32_t stream_phase_tiles = stream_src_endpoint_get_phase_tiles_count(stream_id);
          volatile uint32_t tt_reg_ptr* tiles_received_ptr = (volatile uint32_t tt_reg_ptr*)get_operand_tiles_received_ptr(stream_id_to_operand(stream_id));
          uint16_t operand_tiles_received = (uint16_t)tiles_received_ptr[0];
          uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, stream_phase_tiles);
          tiles_received_ptr[0] = new_epoch_tiles_received;
          curr_active_stream_info->dram_prefetch_stream_need_restart = 0;
        }
      }
    }
  }
}

void risc_all_streams_done_check(
  uint32_t &num_active_streams, active_stream_info_t *active_stream_info
)
{
  bool all_streams_done = false;
  uint32_t stream_restart_check_cnt;
  uint32_t poll_count = 0;
  while (!all_streams_done) {
#ifndef ERISC
    risc_reset_check();
#endif

    all_streams_done = true;

    stream_restart_check_cnt = 0;
    risc_stream_resart_check(stream_restart_check_cnt, num_active_streams, active_stream_info);

    for (uint32_t s = 0; s < num_active_streams; s++) {
      uint32_t flags = active_stream_info[s].flags;
      if ((flags & STREAM_INTERMEDIATE) == 0 && ((flags & STREAM_MOVES_RAW_DATA) == 0 || (flags & STREAM_DRAM_OUTPUT) != 0)) {
        uint32_t stream_id = active_stream_info[s].stream_id;
        bool stream_done = false;
        if (stream_phase_advance_wait(stream_id)) {
          if ((flags & STREAM_MOVES_RAW_DATA) != 0) {
            stream_done = true;
          } else {
            volatile epoch_stream_info_t tt_l1_ptr * stream_info = RISC_EPOCH_INFO_PTR->active_streams[s];
            stream_done = (stream_info->epoch_iters_remaining == 0);
          }
        }
        if (stream_done) {
          if (assert_check(stream_id, false)) {
            volatile uint32_t * tt_l1_ptr test_mailbox_ptr = (volatile uint32_t tt_l1_ptr *)(l1_mem::address_map::FIRMWARE_BASE + TEST_MAILBOX_ADDRESS);
            test_mailbox_ptr[0] = RISC_DETECTED_STREAM_ASSERT;
          }
          stream_reset(stream_id);
        }
        all_streams_done &= stream_done;
#ifdef ERISC
        RISC_POST_STATUS(0x44000000 | (stream_id << 16) | (poll_count & 0xFFFF));
#endif
      }
    }
#ifdef ERISC
    risc_context_switch();
#endif
    poll_count++;
  }
}

void risc_unpack_pack_stream_handler_loop(
  uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
  uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state,
  uint32_t &num_active_streams, active_stream_info_t *active_stream_info,
  bool erisc
) {
  
  uint32_t stream_restart_check_cnt = 1; // I.e. dont check immediately since streams most likely just started
  bool skip_kernels = RISC_EPOCH_INFO_PTR->skip_kernels;

  while (true) {

    bool all_kernels_done;
    if (!erisc) {
      uint32_t trisc0_mailbox = trisc0_mailbox_ptr[0];
      uint32_t trisc1_mailbox = trisc1_mailbox_ptr[0];
      uint32_t trisc2_mailbox = trisc2_mailbox_ptr[0];
      all_kernels_done = skip_kernels || (
        (trisc0_mailbox == KERNEL_COMPLETE) &&
        (trisc1_mailbox == KERNEL_COMPLETE) &&
        (trisc2_mailbox == KERNEL_COMPLETE));

      risc_reset_check();

      risc_stream_resart_check(stream_restart_check_cnt, num_active_streams, active_stream_info);
    } else {
      all_kernels_done = true;
    }

    RISC_POST_STATUS(0xC0000000);

    uint32_t streams_to_clear = 0;

    kernel_input_stream_state_t* curr_input_state = input_stream_state;
#ifdef ERISC
    bool fw_eth_stream_send_tiles_received = false;
#endif
    for (uint32_t input = 0; input < num_input_streams; input++) {
      
      uint32_t stream_id = curr_input_state->stream_id;
      uint32_t epoch_tiles_remaining_to_clear = curr_input_state->epoch_tiles_remaining_to_clear;
      uint32_t no_hw_clearing = curr_input_state->no_hw_clearing;
      uint32_t phase_active = stream_phase_is_active(stream_id);
      uint32_t phase_tiles_remaining_to_clear = stream_rec_endpoint_get_phase_tiles_count(stream_id);

#if defined(PERF_DUMP) && (OVERLAY_DECOUPLE == 1)
      if (phase_active) {
        uint8_t operand_idx = uint8_t(stream_id_to_operand(stream_id));
        if (!perf::first_bw_timestamp_recorded(operand_idx, true)) {
          if (is_input_operand_decoupled(operand_idx)) {
            volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr_tmp = curr_input_state->epoch_tiles_received_ptr;
            if (epoch_tiles_received_ptr_tmp[0] != 0) {
              volatile epoch_stream_info_t tt_l1_ptr * input_stream_info_tmp = RISC_EPOCH_INFO_PTR->inputs[input];
              uint num_tiles_input = input_stream_info_tmp->epoch_num_tiles;
              perf::record_input_start_bw_timestamp(operand_idx, num_tiles_input, epoch_tiles_received_ptr_tmp[0]);
            }
          }
        }
      }
#endif

      curr_input_state++;

      uint32_t stream_skip = (epoch_tiles_remaining_to_clear == 0) || (no_hw_clearing);
      if (stream_skip) {
        continue;
      }

      all_kernels_done = false; // prevent breaking from outer loop

      stream_skip = (phase_active == 0) || (phase_tiles_remaining_to_clear == 0);
      if (stream_skip) {
        continue;
      }

      kernel_input_stream_state_t* prev_input_state = curr_input_state - 1;
      volatile uint32_t tt_reg_ptr* epoch_tiles_acked_ptr = prev_input_state->epoch_tiles_acked_ptr;
      volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr = prev_input_state->epoch_tiles_received_ptr;
      uint16_t prev_phases_tiles_received = prev_input_state->prev_phases_tiles_received;
      uint16_t epoch_tiles_cleared = prev_input_state->epoch_tiles_cleared;
      uint32_t input_idx = prev_input_state->input_idx;
      uint32_t msg_info_buf_addr = RISC_EPOCH_INFO_PTR->inputs[input_idx]->msg_info_buf_start;

      uint16_t epoch_tiles_acked = epoch_tiles_acked_ptr[0];
      uint32_t curr_phase_tiles_received = stream_phase_tiles_received(stream_id, msg_info_buf_addr);

      if (!erisc) {
        uint32_t num_fork_streams = prev_input_state->num_fork_streams;
        if (num_fork_streams > 0) {
          if (prev_input_state->fork_prev_pushed_num_tiles > curr_phase_tiles_received) {
            RISC_POST_STATUS(0xDEAD0001);
            while(true);
          }
          uint32_t num_tiles_u = curr_phase_tiles_received - prev_input_state->fork_prev_pushed_num_tiles;
          if (num_tiles_u > 0) {
            for (uint32_t k = 0; k < num_fork_streams; k++) {
              uint32_t fork_stream_id = prev_input_state->fork_stream_ids[k];
              phase_active &= stream_phase_is_active(fork_stream_id);
              bool fork_next_phase_src_change = stream_next_phase_src_change(fork_stream_id);
              if (fork_next_phase_src_change)
                phase_active &= stream_get_curr_phase_num_msgs(fork_stream_id) > 0;
            }
            if (phase_active == 0) {
              continue;
            }

            uint32_t num_words = special_mult(num_tiles_u, stream_phase_next_recved_tile_size(stream_id));
            for (uint32_t k = 0; k < num_fork_streams; k++) {
              stream_relay_tiles(prev_input_state->fork_stream_ids[k], num_tiles_u, num_words);
            }
            prev_input_state->fork_prev_pushed_num_tiles = curr_phase_tiles_received;
          }
        }
      }

      // It looks like we won't need words_acked but keep this in case we want to revert to
      // firmware-issued flow control remote reg writes.     
#ifndef ERISC
      uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(curr_phase_tiles_received, prev_phases_tiles_received);
      epoch_tiles_received_ptr[0] = (uint32_t)new_epoch_tiles_received;
#else
      uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(curr_phase_tiles_received, prev_phases_tiles_received);
      if (prev_input_state->eth_fw_stream) {
        uint32_t epoch_tiles_relayed = epoch_tiles_received_ptr[0];
        uint32_t msg_rd_addr = prev_input_state->msg_rd_addr;
        uint32_t msg_wr_addr = prev_input_state->msg_wr_addr;
        uint32_t dest_addr = ((uint32_t) epoch_tiles_received_ptr - (uint32_t) sender_tiles_received) & 0x7F;
        dest_addr += (uint32_t) receiver_tiles_received;
        if ((new_epoch_tiles_received > epoch_tiles_relayed)) {
          uint32_t tiles_to_relay = new_epoch_tiles_received - epoch_tiles_relayed;
          uint32_t words_to_relay = mulsi3(tiles_to_relay, RISC_EPOCH_INFO_PTR->inputs[input_idx]->tile_size_words);
          uint32_t space_available = stream_get_remote_data_buf_space_available(stream_id);
          if (words_to_relay > space_available) {
            words_to_relay = space_available;
            tiles_to_relay = words_to_relay / RISC_EPOCH_INFO_PTR->inputs[input_idx]->tile_size_words;
          }
          
          if (words_to_relay) {
            eth_send_packet(0, msg_rd_addr, msg_wr_addr, words_to_relay);
            uint32_t words_to_decrement = -words_to_relay;
            stream_update_remote_dest_buf_space_available(stream_id, 0, words_to_decrement);
            uint16_t temp_sum = op_pack_tiles_ptr_add(tiles_to_relay, epoch_tiles_relayed);
            epoch_tiles_received_ptr[0] = temp_sum;
            prev_input_state->msg_rd_addr = buf_ptr_inc_wrap(msg_rd_addr, words_to_relay, stream_get_data_buf_size(stream_id));
            prev_input_state->msg_wr_addr = buf_ptr_inc_wrap(msg_wr_addr, words_to_relay, stream_get_remote_data_buf_size(stream_id));
            fw_eth_stream_send_tiles_received = true;
          } else if (epoch_tiles_acked == 0) {
            fw_eth_stream_send_tiles_received = true;
          }
        } else if (epoch_tiles_acked == 0) {
          fw_eth_stream_send_tiles_received = true;
        }
      } else {
        epoch_tiles_received_ptr[0] = (uint32_t)new_epoch_tiles_received;
      }
#endif
      uint16_t tiles_to_clear = op_pack_tiles_ptr_sub(epoch_tiles_acked, epoch_tiles_cleared);
      if (tiles_to_clear > phase_tiles_remaining_to_clear)
        tiles_to_clear = phase_tiles_remaining_to_clear;
      if (!tiles_to_clear) {
        continue;
      }
      
#ifdef USE_2K_TILE_HEADER_BUFFER_RESET
      uint32_t num_iter_tiles = prev_input_state->num_iter_tiles;
      uint32_t buf_size_tiles = RISC_EPOCH_INFO_PTR->inputs[input_idx]->buf_size_tiles;
      uint32_t prev_ack_thresh = 0;
      if (should_stall_for_tile_header_buffer_reset(stream_id, msg_info_buf_addr, buf_size_tiles, prev_ack_thresh)) {
        wait_till_tile_clear_done(stream_id);
        if (prev_ack_thresh != 0)
          prev_input_state->prev_ack_thresh = prev_ack_thresh;
        prev_ack_thresh = prev_input_state->prev_ack_thresh;
        uint32_t prev_phases_tiles_received_inc;
        bool tile_header_buffer_reset = reset_tile_header_buffer(stream_id, msg_info_buf_addr, buf_size_tiles, prev_phases_tiles_received_inc, prev_ack_thresh, num_iter_tiles); 
        if (tile_header_buffer_reset) {
          prev_input_state->prev_phases_tiles_received = prev_phases_tiles_received + prev_phases_tiles_received_inc;
          prev_input_state->fork_prev_pushed_num_tiles = 0;
          prev_input_state->prev_ack_thresh = 0;
        } else {
          continue;
        }
      }
      num_iter_tiles -= tiles_to_clear;
      if (num_iter_tiles == 0) {
        num_iter_tiles = RISC_EPOCH_INFO_PTR->inputs[input_idx]->num_iter_tiles;
      }
      prev_input_state->num_iter_tiles = num_iter_tiles;
#endif
      streams_to_clear++;
      epoch_tiles_remaining_to_clear -= tiles_to_clear;
      phase_tiles_remaining_to_clear -= tiles_to_clear;
      epoch_tiles_cleared = op_pack_tiles_ptr_add(epoch_tiles_cleared, tiles_to_clear);
      prev_input_state->tiles_to_clear = tiles_to_clear;
      prev_input_state->epoch_tiles_cleared = epoch_tiles_cleared;
      prev_input_state->epoch_tiles_remaining_to_clear = epoch_tiles_remaining_to_clear;
      stream_rec_endpoint_set_phase_tiles_count(stream_id, phase_tiles_remaining_to_clear);
      bool next_phase_src_change = stream_next_phase_src_change(stream_id);
      if (next_phase_src_change && !phase_tiles_remaining_to_clear) {
        prev_input_state->prev_phases_tiles_received = epoch_tiles_cleared;
        prev_input_state->fork_prev_pushed_num_tiles = 0;
      }
    }

#ifdef ERISC
    if (fw_eth_stream_send_tiles_received) {
      eth_send_packet(0, (uint32_t)sender_tiles_received >> 4, (uint32_t)receiver_tiles_received >> 4, sizeof(sender_tiles_received) >> 4);
    }
#endif
    RISC_POST_STATUS(0xD0000000);

    process_tile_clearing(input_stream_state, streams_to_clear);

    RISC_POST_STATUS(0xE0000000);

    kernel_output_stream_state_t* curr_output_state = output_stream_state;
    for (uint32_t output = 0; output < num_output_streams; output++) {
      
      uint32_t stream_id = curr_output_state->stream_id;
      uint32_t active_streams_idx = curr_output_state->active_streams_idx;
      uint32_t skip_processing = curr_output_state->skip_processing;

      curr_output_state++;

      uint32_t stream_skip = (skip_processing);
      if (stream_skip) {
        continue;
      }

      kernel_output_stream_state_t* prev_output_state = curr_output_state - 1;
      volatile uint32_t tt_reg_ptr* epoch_tiles_acked_ptr = prev_output_state->epoch_tiles_acked_ptr;
      volatile uint32_t tt_reg_ptr* epoch_tiles_received_ptr = prev_output_state->epoch_tiles_received_ptr;
      uint32_t num_fork_streams = prev_output_state->num_fork_streams;
      uint32_t scatter_inner_loop_done = prev_output_state->scatter_inner_loop_done;

      stream_id = get_stream_id(prev_output_state);
      active_streams_idx = get_active_idx(prev_output_state);

#if defined(PERF_DUMP) && (OVERLAY_DECOUPLE == 1)
      if (is_output_operand_decoupled(stream_id_to_operand(stream_id), output_decouple_mask)) {
        volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx];
        if (!(((active_stream_info[active_streams_idx].flags & STREAM_MOVES_RAW_DATA) != 0) || l1_stream_info->legacy_pack)) {
          uint32_t epoch_iters_remaining = active_stream_info[active_streams_idx].epoch_iters_remaining;
          uint32_t total_num_tiles = RISC_EPOCH_INFO_PTR->active_streams[active_streams_idx]->epoch_num_tiles;
          bool stream_done = false;
          if (stream_phase_advance_wait(stream_id)) {
            if (!((active_stream_info[active_streams_idx].flags & STREAM_MOVES_RAW_DATA) != 0)) {
              stream_done = (epoch_iters_remaining == 0);
            }
          }
          if (!stream_done) {
            all_kernels_done = false;
          }

          uint8_t operand_idx = uint8_t(stream_id_to_operand(stream_id));
          uint8_t output_operand_idx = operand_idx - OPERAND_OUTPUT_START_INDEX;
          perf::record_output_start_bw_timestamp(output_operand_idx);
          if (stream_done) {
            perf::record_output_end_bw_timestamp(output_operand_idx);
          }

          uint32_t phase_active = stream_phase_is_active(stream_id) && !is_dummy_phase(stream_id);
          uint16_t epoch_tiles_received = *epoch_tiles_received_ptr;
          uint16_t epoch_tiles_acked = *epoch_tiles_acked_ptr;
          uint16_t num_tiles_recv = op_pack_tiles_ptr_sub(epoch_tiles_received, epoch_tiles_acked);
          if (phase_active && num_tiles_recv == 0) {
            uint16_t operand_tiles_received = (uint16_t)epoch_tiles_received_ptr[0];
            uint16_t new_epoch_tiles_received = op_pack_tiles_ptr_add(operand_tiles_received, prev_output_state->num_msgs_in_block & ~BRISC_PACKER_SENDING_IN_PROGRESS);
            epoch_tiles_received_ptr[0] = new_epoch_tiles_received;
            decoupled_output_num_tiles += (prev_output_state->num_msgs_in_block & ~BRISC_PACKER_SENDING_IN_PROGRESS);
          }
        }
      }
#endif

      for (uint32_t k = 0; k < num_fork_streams+1; k++) {
        uint32_t fork_active_streams_idx = k == 0 ? active_streams_idx : get_fork_idx(prev_output_state, k-1);
        uint32_t fork_stream_id = k == 0 ? stream_id : RISC_EPOCH_INFO_PTR->active_streams[fork_active_streams_idx]->stream_id;
        process_scatter_flush(fork_stream_id, active_stream_info, fork_active_streams_idx, prev_output_state,
                              epoch_tiles_acked_ptr, epoch_tiles_received_ptr, all_kernels_done, k == 0 ? -1 : k-1,
                              scatter_inner_loop_done);
      }

      bool all_scatter_inner_loop_done = is_all_fork_scatter_loop_done(scatter_inner_loop_done, num_fork_streams);
      if (all_scatter_inner_loop_done) {
        for (uint32_t k = 0; k < num_fork_streams+1; k++) {
          uint32_t fork_active_streams_idx = k == 0 ? active_streams_idx : get_fork_idx(prev_output_state, k-1);
          process_pipe_scatter_output_loop(fork_active_streams_idx);
        }

        uint32_t num_msgs_in_block = prev_output_state->num_msgs_in_block;
        uint16_t prev_epoch_tiles_acked = *epoch_tiles_acked_ptr;
        uint16_t new_epoch_tiles_acked = op_pack_tiles_ptr_add(prev_epoch_tiles_acked, num_msgs_in_block);
        *epoch_tiles_acked_ptr = (uint32_t)new_epoch_tiles_acked;
            
#ifdef ERISC
        if (prev_output_state->eth_fw_stream) {
          uint32_t output_idx = prev_output_state->output_idx;
          volatile epoch_stream_info_t tt_l1_ptr * output_stream_info = RISC_EPOCH_INFO_PTR->outputs[output_idx];
          uint32_t remote_eth_stream_id = output_stream_info->eth_remote_fw_stream_id;
          uint32_t words_to_relay = mulsi3(num_msgs_in_block, output_stream_info->tile_size_words);

          eth_write_remote_reg(0, STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE_ADDR(remote_eth_stream_id),
                               STREAM_REMOTE_DEST_BUF_SPACE_AVAILABLE_UPDATE(0, words_to_relay));
          uint32_t dest_addr = ((uint32_t) epoch_tiles_acked_ptr - (uint32_t) receiver_tiles_acked) & 0x7F;
          dest_addr += (uint32_t) sender_tiles_acked;
          eth_send_packet(0, (uint32_t) epoch_tiles_acked_ptr >> 4, dest_addr >> 4, 1);
      }
#endif
        uint32_t mblock_buffering_count = prev_output_state->mblock_buffering_count;
        uint32_t num_mblock_buffering = prev_output_state->num_mblock_buffering;
        mblock_buffering_count++;
        if (mblock_buffering_count >= num_mblock_buffering)
          mblock_buffering_count -= num_mblock_buffering;
        prev_output_state->mblock_buffering_count = mblock_buffering_count;

        prev_output_state->scatter_inner_loop_count = 0;
        if (num_fork_streams > 0) {
          for (uint32_t k = 0; k < num_fork_streams; k++) {
            uint32_t fork_active_streams_idx = get_fork_idx(prev_output_state, k);
            uint32_t fork_stream_id = RISC_EPOCH_INFO_PTR->active_streams[fork_active_streams_idx]->stream_id;
            set_fork_scatter_inner_loop_count(fork_stream_id, 0);
          }
        }

        scatter_inner_loop_done = 0;
        prev_output_state->scatter_inner_loop_done = scatter_inner_loop_done;
      }

      for (uint32_t k = 0; k < num_fork_streams+1; k++) {
        uint32_t fork_active_streams_idx = k == 0 ? active_streams_idx : get_fork_idx(prev_output_state, k-1);
        uint32_t fork_stream_id = k == 0 ? stream_id : RISC_EPOCH_INFO_PTR->active_streams[fork_active_streams_idx]->stream_id;
        process_scatter_send(fork_stream_id, active_stream_info, fork_active_streams_idx, prev_output_state,
                             epoch_tiles_acked_ptr, epoch_tiles_received_ptr, all_kernels_done, k == 0 ? -1 : k-1,
                             scatter_inner_loop_done);
        if (erisc) {
          if (active_stream_info[fork_active_streams_idx].epoch_iters_remaining > 0) {
            all_kernels_done = false;
          }
        }
      }
    }
#ifndef ERISC
    // This is at the end such that if all kernels are done we want to do 1 more clear iteration to ensure all streams are cleared
    if (all_kernels_done) {
      break;
    }
#else
    //erisc does not need the outer loop.
    RISC_EPOCH_INFO_PTR->all_streams_and_kernels_done = all_kernels_done;
    break;
#endif
  }

  RISC_POST_STATUS(0xF0000000);

#ifndef ERISC
  // reset the last phase of intermediate streams that use infinite loop
  for (uint32_t s = 0; s < num_active_streams; s++) {
    uint32_t flags = active_stream_info[s].flags;
    if ((flags & STREAM_INTERMEDIATE) != 0 || ((flags & STREAM_MOVES_RAW_DATA) != 0 && (flags & STREAM_DRAM_OUTPUT) == 0)) {      
      uint32_t stream_id = active_stream_info[s].stream_id;
      while (!stream_phase_is_active(stream_id)) { // This is needed for cases where the while loop above finishes fast, we want to guard against streams that havent been init yet
        risc_reset_check();
      }
      stream_reset(stream_id);
    }
  }

  risc_all_streams_done_check(num_active_streams, active_stream_info);
#endif
  
}


// Debugging packed tiles for cases where entire tile should be same value

//std::uint32_t prev_fifo_wr_ptr = outputs[output].f.fifo_wr_ptr;
//...
//while (regfile[p_gpr_pack::PACK_STREAM_SYNC + output] > (0))
//        ;  // Wait for prev push_tiles to complete write to register
//volatile std::uint32_t * first_val_ptr = (volatile std::uint32_t *)((prev_fifo_wr_ptr << 4) + 16);
//volatile std::uint32_t * last_val_ptr = (volatile std::uint32_t *)((prev_fifo_wr_ptr << 4) + 2080 - 32 - 16);
//std::uint32_t first_val = *first_val_ptr;
//std::uint32_t last_val = *last_val_ptr;
//if (first_val != last_val) {
//    *((volatile std::uint32_t *)(0x8)) = (std::uint32_t)first_val_ptr;
//    *((volatile std::uint32_t *)(0xc)) = (std::uint32_t)last_val_ptr;
//    *((volatile std::uint32_t *)(0x10)) = first_val;
//    *((volatile std::uint32_t *)(0x14)) = last_val;
//    while(true){}
//}

