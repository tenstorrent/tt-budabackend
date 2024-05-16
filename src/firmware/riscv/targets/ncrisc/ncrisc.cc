// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "risc.h"
#include "dram_stream_intf.h"
#include "l1_address_map.h"
#include "dram_address_map.h"
#include "risc_epoch.h"
#include "risc_common.h"
#include "epoch_q.h"
#include "context.h"
#include "tt_log.h"
#ifdef PERF_DUMP
#include "risc_perf.h"
#endif

// NCRISC set of functions
// 1. Epoch loading (check epoch queue in the loop,
//    if there is data loaded by runtime, execute that epoch - load data structures
//    and figure out how many streams needed for dram read/write, load kernels, setup brisc)
// 2. Epoch execution 
//   A. Epoch (regular, as defined in the netlist)
//   B. Epoch management (some of the things runtime would do, delegated to tensix to enable systems like galaxy - allocating queues, updating queue parameters and similar)

// When executing an Epoch
// 1. Prolog loading - any kind of prolog buffer during the epoch initialization, place in the L1
//    so it's constantly available during the epoch (relaxing DRAM bandwidth, useful for weights)
// 2. Dram reads
//   1. Scatter read
//   2. Embedding read / Tilize
//   3. MMIO pipes
// 3. Dram write
//   1. Regular FIFO based writes - No reblocking
//   2. Limited scatter - means you can scatter to multiple different destination dram queues, 
//      with the only caviat being that a particular dram destination can be only fed from a single core.
//   3. Untilize -  Tensor is given to the math engine in packets of tiles  (32x32 data in specific scan order; where tiles are grouped into ublocks and mblocks, in their own scan order)
//      This would make data into row major format in memory - matching what PyTorch/HostCPU would expect as final output.
//      The only place where multiple cores are writing in the same DRAM queue
// 4. Epilogue - once all of the streams are done, and brisc has notified ncrisc that it's done. 
//    Executing Epilogue to reset all streams [that might be hacked].
//    Also copy epilogue buffers into DRAM [opposite of Prolog, L1 to DRAM]
//    wait for all NOC trafic to end, and get another epoch from the epoch queue [step 1].


volatile uint32_t local_mem_barrier;
volatile epoch_t tt_l1_ptr * curr_epoch_info;
volatile uint32_t tt_l1_ptr* test_mailbox_ptr = (volatile uint32_t tt_l1_ptr*)(l1_mem::address_map::NCRISC_FIRMWARE_BASE + 0x4);

int post_index;

volatile uint32_t tt_l1_ptr noc_read_scratch_buf[32] __attribute__((section("data_l1_noinit"))) __attribute__((aligned(NOC_ADDRESS_ALIGNMENT))) ;
volatile uint16_t tt_l1_ptr *debug_mailbox_base = nullptr;
uint8_t mailbox_index = 0;
uint8_t mailbox_end = 32;
uint64_t my_q_table_offset;
uint32_t my_q_rd_ptr;
uint32_t my_q_wr_ptr;
uint8_t my_x[NUM_NOCS];
uint8_t my_y[NUM_NOCS];
uint8_t my_logical_x[NUM_NOCS];
uint8_t my_logical_y[NUM_NOCS];
uint8_t loading_noc;
uint8_t noc_size_x;
uint8_t noc_size_y;
uint8_t noc_trans_table_en;
volatile uint8_t unused0[3]; // We need global variables in .bss to be divisible by 4, otherwise we can get a wzerorange error
uint32_t epoch_empty_check_cnt;

uint32_t noc_reads_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_acked[NUM_NOCS];
uint32_t noc_xy_local_addr[NUM_NOCS];

volatile uint64_t epoch_q_dram_addr __attribute__((section("data_l1_noinit")));

uint32_t num_dram_input_streams   __attribute__((section ("data_noinit"))) ;
uint32_t num_dram_output_streams  __attribute__((section ("data_noinit"))) ;
uint32_t num_active_streams       __attribute__((section ("data_noinit"))) ;
uint32_t num_active_dram_queues   __attribute__((section ("data_noinit"))) ;
uint32_t num_dram_prefetch_streams __attribute__((section ("data_noinit"))) ;

dram_q_state_t tt_l1_ptr    dram_q_state[MAX_L0_DRAM_Q_STATE_T]              __attribute__((section ("data_noinit"))) ;
dram_input_stream_state_t   dram_input_stream_state[MAX_DRAM_INPUT_STREAMS]   __attribute__((section ("data_noinit"))) ;
dram_output_stream_state_t  dram_output_stream_state[MAX_DRAM_OUTPUT_STREAMS] __attribute__((section ("data_noinit"))) ;
active_stream_info_t tt_l1_ptr active_stream_info[NOC_NUM_STREAMS]            __attribute__((section ("data_l1_noinit"))) __attribute__((aligned(32))) ;

epoch_stream_info_t tt_l1_ptr * tt_l1_ptr dram_prefetch_epoch_stream_info[MAX_PREFETCH_STREAMS] __attribute__((section("data_l1_noinit"))) __attribute__((aligned(32))) ;
active_stream_info_t tt_l1_ptr * tt_l1_ptr dram_prefetch_active_stream_info[MAX_PREFETCH_STREAMS] __attribute__((section("data_l1_noinit"))) __attribute__((aligned(32))) ;
#ifndef NUM_EXEC_LOOP_ITERATIONS
#define NUM_EXEC_LOOP_ITERATIONS 1
#endif
static_assert(NUM_EXEC_LOOP_ITERATIONS >= 1, "NUM_EXEC_LOOP_ITERATIONS must be undefined or >= 1");

#ifdef RISC_GSYNC_ENABLED
volatile uint32_t gsync_epoch __attribute__((section("data_l1_noinit"))) __attribute__((aligned(32))) ;
volatile uint32_t epochs_in_progress __attribute__((section("data_l1_noinit"))) __attribute__((aligned(32))) ;
#endif
// Ncrisc does not implement set_risc_reset_vector since Ncrisc requests Brisc
// to reset ncrisc. This function is implemented in Brisc.
void set_risc_reset_vector()
{

}

void risc_initialize_tile_counters(uint32_t num_kernel_inputs, uint32_t num_kernel_outputs)
{
  for (uint32_t i = 0; i < num_kernel_inputs; i++) {
    uint32_t operand = stream_id_to_operand(RISC_EPOCH_INFO_PTR->inputs[i]->stream_id);
    volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_operand_tiles_received_ptr(operand);
    volatile uint32_t tt_reg_ptr * tiles_acked_ptr = get_operand_tiles_acked_ptr(operand);
    tiles_received_ptr[0] = 0;
    tiles_acked_ptr[0] = 0;
  }
  for (uint32_t i = 0; i < num_kernel_outputs; i++) {
    if (!RISC_EPOCH_INFO_PTR->outputs[i]->legacy_pack) {
      uint32_t operand = stream_id_to_operand(RISC_EPOCH_INFO_PTR->outputs[i]->stream_id);
      volatile uint32_t tt_reg_ptr * tiles_received_ptr = get_packer_tiles_received_ptr(operand);
      volatile uint32_t tt_reg_ptr * tiles_acked_ptr = get_packer_tiles_acked_ptr(operand);
      tiles_received_ptr[0] = 0;
      tiles_acked_ptr[0] = 0;
    }
  }
}

uint8_t risc_streams_kernels_done()
{
  if (!RISC_EPOCH_INFO_PTR->all_streams_and_kernels_done)
    return false;

  uint8_t done = 1;
  volatile dram_input_stream_state_t* curr_dram_input_stream_state = dram_input_stream_state;
  volatile dram_output_stream_state_t* curr_dram_output_stream_state = dram_output_stream_state;
  for (uint32_t i = 0;
      i < num_dram_input_streams;
      i++, curr_dram_input_stream_state++) {
    if (curr_dram_input_stream_state->epoch_q_slots_remaining)
    {
      done = 0;
      break;
    }
  }
  for (uint32_t i = 0;
      i < num_dram_output_streams;
      i++, curr_dram_output_stream_state++) {
    if (curr_dram_output_stream_state->epoch_q_slots_remaining)
    {
      done = 0;
      break;
    }
  }
  return done;
}

void risc_epoch_load(uint64_t dram_next_epoch_ptr) {

  uint32_t epoch_size_bytes = l1_mem::address_map::OVERLAY_BLOB_SIZE + l1_mem::address_map::EPOCH_RUNTIME_CONFIG_SIZE;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_next_epoch_ptr + DRAM_EPOCH_RUNTIME_CONFIG_BASE, l1_mem::address_map::EPOCH_RUNTIME_CONFIG_BASE, epoch_size_bytes, NCRISC_RD_DEF_TRID);
  while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));
  *MCAST_PACKER_OPT_EN = EPOCH_INFO_PTR->has_packer_mcast_opt;
  
}

void risc_extra_overlay_blob_load(uint64_t dram_next_epoch_ptr, uint32_t overlay_blob_extra_size) {

  uint64_t extra_blob_base_dram = dram_next_epoch_ptr + DRAM_OVERLAY_BLOB_BASE + l1_mem::address_map::OVERLAY_BLOB_SIZE;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, extra_blob_base_dram, l1_mem::address_map::OVERLAY_BLOB_BASE + l1_mem::address_map::OVERLAY_BLOB_SIZE, overlay_blob_extra_size, NCRISC_RD_DEF_TRID);
  while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));

}

void risc_kernels_load(uint64_t kernels_address) {

  uint32_t kernels_size_bytes = l1_mem::address_map::TRISC0_SIZE + l1_mem::address_map::TRISC1_SIZE + l1_mem::address_map::TRISC2_SIZE;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, kernels_address, l1_mem::address_map::TRISC_BASE, kernels_size_bytes, NCRISC_RD_START_TRID);
  // We dont flush reads as it will be done in risc_stream_handler_init()

}

void __attribute__((section("code_l1"))) __attribute__((noinline)) init_ncrisc_streams(void * pFunction, uint32_t dram_decouple_mask) {
  RISC_POST_STATUS((uint32_t)pFunction);
  num_active_streams = EPOCH_INFO_PTR->num_active_streams;

  active_stream_info_t tt_l1_ptr * curr_active_stream_info = active_stream_info;
  
  for (uint32_t i = 0;
       i < num_active_streams;
       i++, curr_active_stream_info++) {

    epoch_stream_info_t tt_l1_ptr * l1_stream_info = EPOCH_INFO_PTR->active_streams[i];
    uint32_t stream_id = l1_stream_info->stream_id;
    uint32_t flags = l1_stream_info->flags;

    curr_active_stream_info->stream_id = stream_id;
    curr_active_stream_info->flags = flags;
  }
}

inline __attribute__((section("code_l1"))) void record_mailbox_value(uint16_t event_value) {
  if (mailbox_index < mailbox_end) {
    debug_mailbox_base[mailbox_index] = event_value;
    mailbox_index++;
  }
}

inline __attribute__((section("code_l1"))) void record_mailbox_value_with_index(uint8_t index, uint16_t event_value) {
  if (index < mailbox_end) {
    debug_mailbox_base[index] = event_value;
  }
}

inline __attribute__((section("code_l1"))) void allocate_debug_mailbox_buffer() {
  std::int32_t debug_mailbox_addr = l1_mem::address_map::DEBUG_MAILBOX_BUF_BASE + 3*l1_mem::address_map::DEBUG_MAILBOX_BUF_SIZE;
  debug_mailbox_base = reinterpret_cast<volatile uint16_t tt_l1_ptr *>(debug_mailbox_addr);
}

int main(int argc, char *argv[]) {

  disable_lowcache();
  
  init_riscv_context();
  allocate_debug_mailbox_buffer();
  risc_l1_epoch_q_reset();
  test_mailbox_ptr[0] = 0xF; // Indicates to host that l1 epoch queue is ready.
  unused0[0] = 0;
  unused0[1] = 0;
  unused0[2] = 0;
  epoch_empty_check_cnt = 0;

#ifndef PERF_DUMP
  RISC_POST_STATUS(0x10000000);
#endif
  ncrisc_noc_init();

#ifndef PERF_DUMP
  RISC_POST_STATUS(0x10000001);
#endif
  uint32_t epoch_command = epoch_queue::EpochCmdNotValid;
  uint32_t next_command = epoch_queue::EpochCmdNotValid;
  uint32_t dram_decouple_mask = 0;
  bool skip_initial_epoch_dram_load = EPOCH_INFO_PTR->epoch_valid;
  uint64_t dram_next_epoch_ptr;
  uint64_t dram_next_epoch_kernels_ptr;
  bool skip_kernels = skip_initial_epoch_dram_load ? EPOCH_INFO_PTR->skip_kernels : false;
#ifdef PERF_DUMP
  uint32_t epoch_q_empty = 0;
#endif

  risc_init();

  bool llk_tb = ((0 == my_x[0]) && (0 == my_y[0])); // DV-only: core (0,0) will execute risc fw only in LLK TB
  loading_noc = my_x[0] % NUM_NOCS;
  int exec_iteration = NUM_EXEC_LOOP_ITERATIONS; // initialize to not inside of a loop iteration

  // Global loop stack
  LoopStack<4> loopstack;

#ifdef PERF_DUMP
  call_with_cpu_flush((void *)risc::allocate_perf_buffer, 0);
#endif

#ifdef RISC_GSYNC_ENABLED
  global_sync_init(gsync_epoch, epochs_in_progress);
#endif
  if (!llk_tb) {
    // process epoch loading
    // get epoch queue pointer
    uint32_t my_x_trans = noc_trans_table_en ? my_logical_x[0] : my_x[0];
    uint32_t my_y_trans = noc_trans_table_en ? my_logical_y[0] : my_y[0];
    my_q_table_offset = risc_get_epoch_q_dram_ptr(my_x_trans, my_y_trans);
    epoch_q_dram_addr = my_q_table_offset;
    risc_epoch_q_get_ptr(noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, my_q_wr_ptr);
    do {
      // if the epoch is not empty, process it
      if (!risc_is_epoch_q_empty(noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, my_q_wr_ptr, epoch_empty_check_cnt) || exec_iteration < NUM_EXEC_LOOP_ITERATIONS) {
        uint32_t num_loops_cmd = 0;
        if (exec_iteration >= NUM_EXEC_LOOP_ITERATIONS) {
#if defined(PERF_DUMP)
          if (epoch_q_empty == 1) {
            epoch_q_empty = 0;
            risc::record_timestamp(risc::perf_event::EPOCH_Q_EMPTY);
          }
#endif
          risc_get_epoch_dram_ptrs(next_command, dram_decouple_mask, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, num_loops_cmd, dram_next_epoch_ptr, dram_next_epoch_kernels_ptr);
          RISC_POST_DEBUG(dram_next_epoch_ptr>>32);
          RISC_POST_DEBUG(dram_next_epoch_ptr & 0xFFFFFFFF);
          epoch_command = next_command;
          if (epoch_queue::EpochCmdValid == epoch_command) {
            exec_iteration = 1;
          }
        } else if (epoch_queue::EpochCmdValid == epoch_command) {
          exec_iteration++;
        }

        uint32_t loop_iteration = loopstack.is_empty() ? 0 : loopstack.top()->curr_iter;

        // different runtime executing commands
        if (epoch_queue::EpochCmdValid == epoch_command) {
          RISC_POST_STATUS(0x10000100 | exec_iteration);
#if OVERLAY_DECOUPLE == 1
          risc::wait_for_all_epoch_commands_sent_signal();
#endif
          // this executes epoch itself
          run_epoch(
              risc_epoch_load, risc_kernels_load, risc_extra_overlay_blob_load, init_ncrisc_streams,
              skip_initial_epoch_dram_load, dram_next_epoch_ptr, dram_next_epoch_kernels_ptr, skip_kernels,
          #ifdef RISC_GSYNC_ENABLED
              gsync_epoch, epochs_in_progress,
          #endif
              num_dram_input_streams, num_dram_output_streams, num_active_streams, num_active_dram_queues, num_dram_prefetch_streams,
              dram_q_state, dram_input_stream_state, dram_output_stream_state, active_stream_info,
              dram_prefetch_epoch_stream_info, dram_prefetch_active_stream_info, dram_decouple_mask
          );
        } else if (epoch_queue::EpochCmdIOQueueUpdate == epoch_command) {
            RISC_POST_STATUS(0x10000101);
            bool varinst_cmd = false;
#if defined(RISC_B0_HW)
            run_dram_queue_update(0, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, dram_next_epoch_ptr, loading_noc, varinst_cmd, loop_iteration);
#else
            call_with_cpu_flush_args2((void *)run_dram_queue_update,
                                      (void *) noc_read_scratch_buf, (void *) &my_q_table_offset, (void *) &my_q_rd_ptr, (void *) &dram_next_epoch_ptr, (void *) &loading_noc, &varinst_cmd, &loop_iteration
            );
#endif
        } else if (epoch_queue::EpochCmdVarinst == epoch_command) {
          RISC_POST_STATUS(0x11000101);
          bool varinst_cmd = true;
#if defined(RISC_B0_HW)
          run_dram_queue_update(0, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, dram_next_epoch_ptr, loading_noc, varinst_cmd, loop_iteration);
#else
          call_with_cpu_flush_args2((void *)run_dram_queue_update,
                                    (void *) noc_read_scratch_buf, (void *) &my_q_table_offset, (void *) &my_q_rd_ptr, (void *) &dram_next_epoch_ptr, (void *) &loading_noc, &varinst_cmd, &loop_iteration
          );
#endif
        } else if (epoch_queue::EpochCmdLoopStart == epoch_command) {
          // Push this loop to loop stack, rdptr will point to next instruction.
          uint32_t loop_start_rd_ptr = dram_io_incr_ptr(my_q_rd_ptr, 1, EPOCH_QUEUE_NUM_SLOTS);
          loopstack.push({.last_iter = num_loops_cmd-1, .epoch_ptr = loop_start_rd_ptr});
        } else if (epoch_queue::EpochCmdLoopEnd == epoch_command) {
            // Jump back to start of loop if more iterations, otherwise pop this loop from stack.
            auto *curr_loop = loopstack.top();
            if (curr_loop->curr_iter < curr_loop->last_iter) {
                curr_loop->curr_iter++;
                my_q_rd_ptr = curr_loop->epoch_ptr;
                // Flush, same as end of program. To ensure rd ptr flushed to DRAM.
                while (!ncrisc_noc_nonposted_writes_flushed(loading_noc, NCRISC_WR_DEF_TRID));
                continue;
            } else {
                loopstack.pop();
            }
        }

        epoch_empty_check_cnt = 0;

        // check if there are more epochs to execute, if yes go back to the loop
        if (exec_iteration == NUM_EXEC_LOOP_ITERATIONS) {
          RISC_POST_DEBUG(0x10000005);
          my_q_rd_ptr = dram_io_incr_ptr(my_q_rd_ptr, 1, EPOCH_QUEUE_NUM_SLOTS);

          // Only update rdptr in L1 and DRAM when all generic-looping-on-device loops have been exited.
          if (loopstack.is_empty()){
            risc_l1_epoch_q_rdptr_update(my_q_rd_ptr);
            risc_epoch_q_rdptr_update(my_q_rd_ptr, noc_read_scratch_buf, my_q_table_offset);
          }
        }

#ifndef PERF_DUMP
        RISC_POST_STATUS(0x10000006);
#endif
        // Special epoch end command, where runtime can directly notify ncrisc to stop execution,
        // where it goes into while loop and waits for reset. 
        if (epoch_queue::EpochCmdEndProgram == epoch_command) {
          // Make sure all pending NOC writes, particularly my_q_rd_ptr update
          // above have been flushed before signaling end of program.
          while (!ncrisc_noc_nonposted_writes_flushed(loading_noc, NCRISC_WR_DEF_TRID));
          EPOCH_INFO_PTR->end_program = 1;
#ifndef PERF_DUMP      
          RISC_POST_STATUS(0x10000007);
#endif
          break;
        }
#ifdef PERF_DUMP
        call_with_cpu_flush((void *)risc::allocate_perf_buffer, 0);
#endif
      }
#if defined(PERF_DUMP)
      else {
        if (epoch_q_empty == 0) {
          epoch_q_empty = 1;
          risc::record_timestamp(risc::perf_event::EPOCH_Q_EMPTY);
        }
      }
#endif
    } while (true);
  } else {
#ifndef PERF_DUMP
    RISC_POST_STATUS(0x10000010);
#endif
    call_with_cpu_flush((void *)init_ncrisc_streams, 0);
    call_with_cpu_flush_args((void *)risc_dram_stream_handler_init_l1,
#ifdef RISC_GSYNC_ENABLED
            (void *) &gsync_epoch, (void *) &epochs_in_progress,
#endif
            (void *) &num_dram_input_streams, (void *) &num_dram_output_streams, (void *) &num_active_streams, (void *) &num_active_dram_queues, (void *) &num_dram_prefetch_streams,
            (void *) dram_q_state, (void *) dram_input_stream_state, (void *) dram_output_stream_state, (void *) active_stream_info,
            (void *) dram_prefetch_epoch_stream_info, (void *) dram_prefetch_active_stream_info
          );
    deassert_trisc_reset();
#ifndef PERF_DUMP
    RISC_POST_STATUS(0x10000020);
#endif
    risc_dram_stream_handler_loop(
#ifdef RISC_GSYNC_ENABLED
      gsync_epoch, epochs_in_progress,
#endif
      num_dram_input_streams, num_dram_output_streams, num_active_streams, num_active_dram_queues, num_dram_prefetch_streams,
      dram_q_state, dram_input_stream_state, dram_output_stream_state, active_stream_info,
      dram_prefetch_epoch_stream_info, dram_prefetch_active_stream_info
    );
    call_with_cpu_flush_args((void *)risc_dram_stream_handler_epilogue_l1,
#ifdef RISC_GSYNC_ENABLED
      (void *) &gsync_epoch, (void *) &epochs_in_progress,
#endif
      (void *) &num_dram_input_streams, (void *) &num_dram_output_streams, (void *) &num_active_streams, (void *) &num_active_dram_queues, (void *) &num_dram_prefetch_streams,
      (void *) dram_q_state, (void *) dram_input_stream_state, (void *) dram_output_stream_state, (void *) active_stream_info,
      (void *) dram_prefetch_epoch_stream_info, (void *) dram_prefetch_active_stream_info
    );
    assert_trisc_reset();
#ifndef PERF_DUMP
    RISC_POST_STATUS(0x10000030);
#endif
    EPOCH_INFO_PTR->end_program = 1;
#ifndef PERF_DUMP
    RISC_POST_STATUS(0x10000040);
#endif
  }
#ifndef PERF_DUMP
  RISC_POST_STATUS(0x1FFFFFF1);
#endif

  test_mailbox_ptr[0] = 0x1;
  
  while (true);
  return 0;
}
