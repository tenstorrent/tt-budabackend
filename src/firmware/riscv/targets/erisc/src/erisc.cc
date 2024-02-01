
#include "dram_stream_intf.h"
#include "noc_overlay_parameters.h"
#include "noc_wrappers.h"
#include "epoch.h"
#include "eth_l1_address_map.h"
#include "stream_interface.h"
#include "risc_epoch.h"
#include "tensix.h"
#include "eth_init.h"
#include "unpack_pack_stream_intf.h"
#include "eth_routing_v2.h"

extern uint32_t __erisc_jump_table;

volatile uint local_mem_barrier;
volatile epoch_t tt_l1_ptr * curr_epoch_info;
volatile uint32_t tt_l1_ptr * test_mailbox_ptr = (volatile uint32_t tt_l1_ptr *)(eth_l1_mem::address_map::FIRMWARE_BASE + 0x4);

int post_index;

void (*rtos_context_switch_ptr)();
volatile uint32_t *RtosTable = (volatile uint32_t *) &__erisc_jump_table;    //Rtos Jump Table. Runtime application needs rtos function handles.;

#ifndef NUM_EXEC_LOOP_ITERATIONS
#define NUM_EXEC_LOOP_ITERATIONS 1
#endif
static_assert(NUM_EXEC_LOOP_ITERATIONS >= 1, "NUM_EXEC_LOOP_ITERATIONS must be undefined or >= 1");

#define NOC_X(x) (loading_noc == 0 ? (x) : (noc_size_x-1-(x)))
#define NOC_Y(y) (loading_noc == 0 ? (y) : (noc_size_y-1-(y)))

volatile uint32_t noc_read_scratch_buf[32] __attribute__((aligned(32))) ;
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
uint32_t epoch_empty_check_cnt;

uint32_t noc_reads_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_acked[NUM_NOCS];
uint32_t noc_xy_local_addr[NUM_NOCS];

uint32_t num_dram_input_streams   __attribute__((section ("data_noinit"))) ;
uint32_t num_dram_output_streams  __attribute__((section ("data_noinit"))) ;
uint32_t num_active_streams       __attribute__((section ("data_noinit"))) ;
uint32_t num_active_dram_queues   __attribute__((section ("data_noinit"))) ;
uint32_t num_dram_prefetch_streams __attribute__((section ("data_noinit"))) ;

dram_q_state_t tt_l1_ptr    dram_q_state[MAX_L0_DRAM_Q_STATE_T]              __attribute__((section ("data_noinit"))) ;
dram_input_stream_state_t   dram_input_stream_state[MAX_DRAM_INPUT_STREAMS-1]   __attribute__((section ("data_noinit"))) ;
dram_output_stream_state_t  dram_output_stream_state[MAX_DRAM_OUTPUT_STREAMS-1] __attribute__((section ("data_noinit"))) ;
active_stream_info_t        active_stream_info[ETH_NOC_NUM_STREAMS]           __attribute__((section ("data_noinit"))) __attribute__((aligned(32))) ;

epoch_stream_info_t* dram_prefetch_epoch_stream_info[MAX_PREFETCH_STREAMS] __attribute__((section("data_noinit"))) __attribute__((aligned(32))) ;
active_stream_info_t* dram_prefetch_active_stream_info[MAX_PREFETCH_STREAMS] __attribute__((section("data_noinit"))) __attribute__((aligned(32))) ;

uint32_t sender_tiles_received[32] __attribute__((section ("data_noinit"))) __attribute__((aligned(16)));
uint32_t sender_tiles_acked[32] __attribute__((section ("data_noinit"))) __attribute__((aligned(16)));
uint32_t receiver_tiles_received[32] __attribute__((section ("data_noinit"))) __attribute__((aligned(16)));
uint32_t receiver_tiles_acked[32] __attribute__((section ("data_noinit"))) __attribute__((aligned(16)));
static uint32_t num_input_streams  __attribute__((section ("data_noinit"))) ;
static kernel_input_stream_state_t input_stream_state[EPOCH_MAX_INPUTS]  __attribute__((section ("data_noinit"))) ;

static uint32_t num_output_streams  __attribute__((section ("data_noinit"))) ;
static kernel_output_stream_state_t output_stream_state[EPOCH_MAX_OUTPUTS_ETH]  __attribute__((section ("data_noinit"))) ;

volatile uint32_t epoch_id_to_send[4] __attribute__((section("data_noinit"))) __attribute__((aligned(16))) ;
volatile uint32_t other_chip_epoch_id[4] __attribute__((section("data_noinit"))) __attribute__((aligned(16))) ;

void __attribute__((section("code_l1"))) init_erisc_streams(void * pFunction, uint32_t dram_decouple_mask) {
  RISC_POST_STATUS((uint32_t)pFunction);

  num_active_streams = RISC_EPOCH_INFO_PTR->num_active_streams;

  volatile active_stream_info_t* curr_active_stream_info = active_stream_info;
  for (uint32_t i = 0;
       i < num_active_streams;
       i++, curr_active_stream_info++) {
    epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
    uint32_t stream_id = l1_stream_info->stream_id;
    uint32_t start_phase_num_cfg_regs = l1_stream_info->start_phase_num_cfg_regs;
    uint32_t flags = l1_stream_info->flags;
    uint32_t epoch_iters_remaining = l1_stream_info->epoch_iters_remaining;

    curr_active_stream_info->stream_id = stream_id;
    curr_active_stream_info->dram_prefetch_stream_need_restart = 0;
    curr_active_stream_info->start_phase_num_cfg_regs = start_phase_num_cfg_regs;
    curr_active_stream_info->flags = flags;
    curr_active_stream_info->epoch_iters_remaining = epoch_iters_remaining; 
    curr_active_stream_info->active_streams_idx = i;
#ifdef DRAM_DECOUPLE
    // Erisc does not support dram decoupling.
    curr_active_stream_info->dram_decoupled = 0;
#endif
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

void __attribute__((section("code_l1"))) risc_context_switch()
{
    ncrisc_noc_full_sync();
    rtos_context_switch_ptr();
    ncrisc_noc_counters_init();
}

void risc_epoch_load(uint64_t dram_next_epoch_ptr) {

  uint32_t epoch_size_bytes = eth_l1_mem::address_map::OVERLAY_BLOB_SIZE;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, dram_next_epoch_ptr, eth_l1_mem::address_map::OVERLAY_BLOB_BASE, epoch_size_bytes, NCRISC_RD_DEF_TRID);
  while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));
  *MCAST_PACKER_OPT_EN = RISC_EPOCH_INFO_PTR->has_packer_mcast_opt;
  
}

void risc_extra_overlay_blob_load(uint64_t dram_next_epoch_ptr, uint32_t overlay_blob_extra_size) {

  uint64_t extra_blob_base_dram = dram_next_epoch_ptr + eth_l1_mem::address_map::OVERLAY_BLOB_SIZE;
  ncrisc_noc_fast_read_any_len(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, extra_blob_base_dram, eth_l1_mem::address_map::OVERLAY_BLOB_BASE + eth_l1_mem::address_map::OVERLAY_BLOB_SIZE, overlay_blob_extra_size, NCRISC_RD_DEF_TRID);
  while (!ncrisc_noc_reads_flushed(loading_noc, NCRISC_RD_DEF_TRID));

}

#ifdef RISC_B0_HW
void __attribute__((section("code_l1"))) risc_iram_load() {
  volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
  volatile system_connection_t* sys_connections = (volatile system_connection_t*)(ETH_CONN_INFO_ADDR);
  volatile uint32_t * iram_load_reg = (volatile uint32_t *)(ETH_CTRL_REGS_START + ETH_CORE_IRAM_LOAD);
  uint32_t my_eth_id;
  if (my_x[0] < 5) {
    my_eth_id = (my_x[0] * 2 - 1) + (my_y[0] == 0 ? 0 : 8);
  } else {
    my_eth_id = (9 - my_x[0]) * 2 + (my_y[0] == 0 ? 0 : 8);
  }

  bool connected = sys_connections->eth_conn_info[my_eth_id] > ETH_UNCONNECTED;
  if (connected) {
    //disable mac
    eth_mac_reg_write(0x0, 0x30000000); // tx disable
    eth_mac_reg_write(0x4, 0x6); // rx disable
  }

  //Trigger copy of code from L1 to IRAM.
  *iram_load_reg = eth_l1_mem::address_map::TILE_HEADER_BUFFER_BASE >> 4;
  RISC_POST_STATUS(0x10000000);
  //wait for code copy to iram to finish
  while (*iram_load_reg & 0x1);

  if (connected) {
    uint32_t aiclk_ps = config_buf->aiclk_ps;
    if (aiclk_ps == 0) aiclk_ps = 1;
    uint8_t clk_scalar = BOOT_REFCLK_PERIOD_PS / (aiclk_ps);
    uint32_t wait_cycles_1us = 27 * clk_scalar; //~1us
    for (int i = 0; i < 100; i++) {
      eth_wait_cycles(wait_cycles_1us);
    }

    while (1) {
      uint32_t pcs_status = eth_pcs_ind_reg_read(0x30001);
      if (pcs_status != 0x6) {
        risc_context_switch();
      } else {
        eth_mac_reg_write(0x0, 0x30000001); // tx enable
        eth_mac_reg_write(0x4, 0x7); // rx enable + drop crc/pad
        break;
      }
    }
  }
}
#endif

void risc_initialize_tile_counters(uint32_t num_kernel_inputs, uint32_t num_kernel_outputs)
{
  for (uint32_t i = 0; i < 32; i++) {
    sender_tiles_received[i] = 0;
    sender_tiles_acked[i] = 0;
    receiver_tiles_received[i] = 0;
    receiver_tiles_acked[i] = 0;
  }
}

void __attribute__((section("erisc_l1_code"))) ApplicationHandler(void)
{
  rtos_context_switch_ptr = (void (*)())RtosTable[0];
  uint32_t epoch_command = epoch_queue::EpochCmdNotValid;
  uint32_t next_command = epoch_queue::EpochCmdNotValid;
  uint32_t dram_decouple_mask = 0;
  uint64_t dram_next_epoch_ptr;
  uint16_t epoch_command_count = 0;

  epoch_empty_check_cnt = 0;

  epoch_id_to_send[0] = 0;
  other_chip_epoch_id[0] = 0;

  ncrisc_noc_init();

  risc_init();

#ifdef RISC_B0_HW
  //copy code from temporary L1 location to erisc iram.
  risc_iram_load();
#endif

  loading_noc = my_x[0] % NUM_NOCS;

  // Global loop stack
  LoopStack<4> loopstack;

  uint8_t x = my_x[0];
  uint8_t y = my_y[0];


  bool skip_initial_epoch_dram_load = RISC_EPOCH_INFO_PTR->epoch_valid;
  my_q_table_offset = risc_get_epoch_q_dram_ptr(x, y);

  noc_read_scratch_buf[30] = my_q_table_offset >> 32;
  noc_read_scratch_buf[31] = my_q_table_offset & 0xFFFFFFFF;

  risc_epoch_q_get_ptr(noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, my_q_wr_ptr);
  int exec_iteration = NUM_EXEC_LOOP_ITERATIONS;

  do {
    if (!risc_is_epoch_q_empty(noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, my_q_wr_ptr, epoch_empty_check_cnt) || exec_iteration < NUM_EXEC_LOOP_ITERATIONS) {
      uint32_t num_loops_cmd = 0;
      if (exec_iteration >= NUM_EXEC_LOOP_ITERATIONS) {
        dram_next_epoch_ptr = risc_get_epoch_dram_ptr(next_command, dram_decouple_mask, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, num_loops_cmd);

        RISC_POST_DEBUG(dram_next_epoch_ptr>>32);
        RISC_POST_DEBUG(dram_next_epoch_ptr & 0xFFFFFFFF);
        epoch_command = next_command;
        epoch_command_count++;

        if (epoch_queue::EpochCmdValid == epoch_command) {
          exec_iteration = 1;
        }
      } else if (epoch_queue::EpochCmdValid == epoch_command) {
        exec_iteration++;
      }

      uint32_t loop_iteration = loopstack.is_empty() ? 0 : loopstack.top()->curr_iter;

      if (epoch_queue::EpochCmdValid == epoch_command) {
        RISC_POST_STATUS(0x10000100 | exec_iteration);
        bool skip_kernels = true;
        run_epoch(
            risc_epoch_load, risc_epoch_load, risc_extra_overlay_blob_load, init_erisc_streams,
            skip_initial_epoch_dram_load, dram_next_epoch_ptr, skip_kernels,
        #ifdef RISC_GSYNC_ENABLED
            gsync_epoch, epochs_in_progress,
        #endif
            epoch_id_to_send[0], other_chip_epoch_id[0],
            num_input_streams, input_stream_state,
            num_output_streams, output_stream_state,
            num_dram_input_streams, num_dram_output_streams, num_active_streams, num_active_dram_queues, num_dram_prefetch_streams,
            dram_q_state, dram_input_stream_state, dram_output_stream_state, active_stream_info,
            dram_prefetch_epoch_stream_info, dram_prefetch_active_stream_info, dram_decouple_mask
        );
        RISC_EPOCH_INFO_PTR->all_streams_and_kernels_done = 0;
      } else if (epoch_queue::EpochCmdIOQueueUpdate == epoch_command) {
          RISC_POST_STATUS(0x10000101);
          bool varinst_cmd = false;
          run_dram_queue_update(0, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, dram_next_epoch_ptr, loading_noc, varinst_cmd, loop_iteration);
      } else if (epoch_queue::EpochCmdVarinst == epoch_command) {
          RISC_POST_STATUS(0x11000101);
          bool varinst_cmd = true;
          run_dram_queue_update(0, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, dram_next_epoch_ptr, loading_noc, varinst_cmd, loop_iteration); // Need to ensure we don't need ifdefs here for A0 vs B0 HW
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

      RISC_POST_DEBUG(0x10000005 | (exec_iteration << 12));

      if (exec_iteration == NUM_EXEC_LOOP_ITERATIONS) {
        my_q_rd_ptr = dram_io_incr_ptr(my_q_rd_ptr, 1, EPOCH_QUEUE_NUM_SLOTS);
        // Only update rdptr in L1 and DRAM when all generic-looping-on-device loops have been exited.
        if (loopstack.is_empty()){
          risc_l1_epoch_q_rdptr_update(my_q_rd_ptr);
          risc_epoch_q_rdptr_update(my_q_rd_ptr, noc_read_scratch_buf, my_q_table_offset);
        }
        RISC_POST_STATUS(0x10000006);
      }

      if (epoch_queue::EpochCmdEndProgram == epoch_command) {
        // Make sure all pending NOC writes, particularly my_q_rd_ptr update
        // above have been flushed before signaling end of program.
        while (!ncrisc_noc_nonposted_writes_flushed(loading_noc, NCRISC_WR_DEF_TRID));
        RISC_EPOCH_INFO_PTR->end_program = 1;
        RISC_POST_STATUS(0x1FFFFFF1);
        break;
      }
    } else {
      RISC_POST_STATUS(0x1BC2DEAD);
      risc_context_switch();
    }
  } while (true);
}
