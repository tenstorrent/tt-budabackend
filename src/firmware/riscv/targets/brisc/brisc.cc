// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

/** @file @brief Main firmware code */

#include "risc.h"
#include <unistd.h>

#include <cstdint>
#include "l1_address_map.h"
#include "dram_address_map.h"

#include "risc_common.h"
#include "noc_helper.h"
#include "noc_overlay_parameters.h"
#include "ckernel_structs.h"
#include "stream_interface.h"
#include "unpack_pack_stream_intf.h"
#include "epoch.h"
#include "c_tensix_core.h"
#include "tdma_xmov.h"
#include "tt_log.h"

#ifdef PERF_DUMP
#include "brisc_perf.h"
#endif

// BRISC primary focus is augmentation of Overlay HW capabilities
// Essentially managing execution for Scatter packing and Legacy packing
// 
// Program structure:
// 1. Init epoch/streams
// 2. Loop
//    1. Checking streams, see if they are done and restarting them
//    2. Receiving tiles, notifying tiles received, and clearing tiles for the unpacker

c_tensix_core core;

volatile uint32_t* instrn_buf[MAX_THREADS];
volatile uint32_t* pc_buf[MAX_THREADS];
volatile uint32_t* mailbox[MAX_THREADS];

static uint32_t num_input_streams  __attribute__((section ("data_noinit"))) ;
static kernel_input_stream_state_t input_stream_state[EPOCH_MAX_INPUTS]  __attribute__((section ("data_noinit"))) ;

static uint32_t num_output_streams  __attribute__((section ("data_noinit"))) ;
static kernel_output_stream_state_t output_stream_state[EPOCH_MAX_OUTPUTS]  __attribute__((section ("data_noinit"))) ;

static uint32_t num_active_streams  __attribute__((section ("data_noinit"))) ;
static active_stream_info_t active_stream_info[NOC_NUM_STREAMS]  __attribute__((section ("data_noinit"))) ;

volatile uint32_t local_mem_barrier;

uint8_t my_x[NUM_NOCS];
uint8_t my_y[NUM_NOCS];
uint8_t my_logical_x[NUM_NOCS];
uint8_t my_logical_y[NUM_NOCS];
uint8_t loading_noc;
uint8_t noc_size_x;
uint8_t noc_size_y;
uint8_t noc_trans_table_en;

int post_index;

#ifdef PERF_DUMP
volatile uint32_t tt_l1_ptr *perf_buf_base;
uint16_t perf_index = 0;
uint16_t perf_end;
uint8_t thread_id = 4;
uint16_t perf_epoch_id;
bool first_timestamp_recorded_input[PERF_MAX_NUM_INPUTS];
bool first_timestamp_recorded_output[PERF_MAX_NUM_OUTPUTS];
bool last_timestamp_recorded_input[PERF_MAX_NUM_INPUTS];
bool last_timestamp_recorded_output[PERF_MAX_NUM_OUTPUTS];
#ifdef OVERLAY_DECOUPLE
uint8_t output_decouple_mask = 0;
uint32_t decoupled_output_num_tiles = 0;
#endif
#endif

bool staggered_start_enabled()  {
  uint32_t soft_reset_0 = READ_REG(RISCV_DEBUG_REG_SOFT_RESET_0);
  return soft_reset_0 & (1u << 31);
}


void stagger_startup() {
  if (staggered_start_enabled()) {
    const uint32_t NOC_ID_MASK = (1 << NOC_ADDR_NODE_ID_BITS) - 1;
    uint32_t noc_id = noc_local_node_id() & 0xFFF;
    uint32_t noc_id_x = noc_id & NOC_ID_MASK;
    uint32_t noc_id_y = (noc_id >> NOC_ADDR_NODE_ID_BITS) & NOC_ID_MASK;

    uint32_t flat_id = (noc_id_y - 1) * 12 + (noc_id_x - 1);
    // To stagger 120 cores by 500us at 1.5GHz works out to 6250 AICLK per core.
    // Use an easy-to-multiply constant close to that.
    uint32_t delay = flat_id * ((1 << 12) | (1 << 11));

    uint64_t end = core.read_wall_clock() + delay;

    while (core.read_wall_clock() < end) { /* empty */ }
  }
}

void disable_thcon_clock_gating() {
#ifdef RISC_BLACKHOLE_HW
  #define CG_CTRL_EN_Thcon_MASK 0x400
  *((uint32_t volatile *)RISCV_DEBUG_REG_CG_CTRL_EN) &= ~CG_CTRL_EN_Thcon_MASK;
#else
  uint32_t pm_mask = 0xFFFF & (~CG_CTRL_EN_Thcon_MASK);
  core.ex_setc16(CG_CTRL_EN_Regblocks_ADDR32, pm_mask, instrn_buf[0]);
#endif
} 

void enable_power_management() {
  // Mask and Hyst taken from tb_tensix math_tests
  uint32_t pm_mask = 0xFFFF;
  uint32_t pm_hyst = 32;
#ifdef RISC_BLACKHOLE_HW
  {
    uint32_t hyst0_reg_data = ((pm_hyst) << 24) | ((pm_hyst) << 16) | ((pm_hyst) << 8) | pm_hyst;
    uint32_t hyst1_reg_data = ((pm_hyst) << 24) | ((pm_hyst) << 16) | ((pm_hyst) << 8) | pm_hyst;
    uint32_t hyst2_reg_data = ((pm_hyst) << 24) | ((pm_hyst) << 16) | ((pm_hyst) << 8) | pm_hyst;

    *((uint32_t volatile *)RISCV_DEBUG_REG_CG_CTRL_HYST0) = hyst0_reg_data;
    *((uint32_t volatile *)RISCV_DEBUG_REG_CG_CTRL_HYST1) = hyst1_reg_data;
    *((uint32_t volatile *)RISCV_DEBUG_REG_CG_CTRL_HYST2) = hyst2_reg_data;
  }                                                                                  
  /*FIXME: need to deal with srcb ctrl bit not fitting in 16 bits. For  */           
  /*now just always turn it on */                                                    
  *((uint32_t volatile *)RISCV_DEBUG_REG_CG_CTRL_EN) = 0x10000 | (pm_mask);
#else
  {
    // Important: program hyteresis first then enable, otherwise the en_pulse will fail to latch the value
    uint32_t hyst_val = pm_hyst & 0x7f;
    
    // Program slightly off values for each CG
    uint32_t hyst0_reg_data = ((hyst_val) << 24) | ((hyst_val) << 16) | ((hyst_val) << 8) | hyst_val;
    uint32_t hyst1_reg_data = ((hyst_val) << 24) | ((hyst_val) << 16) | ((hyst_val) << 8) | hyst_val;
    uint32_t hyst2_reg_data = ((hyst_val) << 24) | ((hyst_val) << 16) | ((hyst_val) << 8) | hyst_val;

    // Force slightly off values for each CG
    // uint32_t hyst0_reg_data = ((hyst_val+3) << 24) | ((hyst_val+2) << 16) | ((hyst_val+1) << 8) | (hyst_val+0);
    // uint32_t hyst1_reg_data = ((hyst_val-4) << 24) | ((hyst_val-3) << 16) | ((hyst_val-2) << 8) | (hyst_val-1);
    // uint32_t hyst2_reg_data = ((hyst_val-6) << 24) | ((hyst_val-5) << 16) | ((hyst_val+5) << 8) | (hyst_val+4);
    WRITE_REG(RISCV_DEBUG_REG_CG_CTRL_HYST0, hyst0_reg_data);
    WRITE_REG(RISCV_DEBUG_REG_CG_CTRL_HYST1, hyst1_reg_data);
    WRITE_REG(RISCV_DEBUG_REG_CG_CTRL_HYST2, hyst2_reg_data);
  }
  
  // core.ex_setc16(CG_CTRL_EN_Hyst_ADDR32, command_data[1] >> 16, instrn_buf[0]);
  core.ex_setc16(CG_CTRL_EN_Regblocks_ADDR32, pm_mask, instrn_buf[0]);
#endif

  if (((pm_mask & 0x0100) >> 8) == 1) {// enable noc clk gatting

    uint32_t hyst_val = pm_hyst & 0x7f;

    //FFB4_0090 - set bit 0 (overlay clkgt en)
    core.write_stream_register(0, STREAM_PERF_CONFIG_REG_INDEX,
                           pack_field(        1, CLOCK_GATING_EN_WIDTH, CLOCK_GATING_EN) |
                           pack_field( hyst_val, CLOCK_GATING_HYST_WIDTH, CLOCK_GATING_HYST) |
                           // XXX: This is a performance optimization for relay streams, not power management related
                           pack_field(       32, PARTIAL_SEND_WORDS_THR_WIDTH, PARTIAL_SEND_WORDS_THR));

    //FFB2_0100 - set bit 0 (NOC0 NIU clkgt en)
    uint32_t oldval;
    oldval = NOC_READ_REG(NOC0_REGS_START_ADDR+0x100);
    oldval = (oldval&0xFFFFFF00) | 1 | (hyst_val <<1);
    NOC_WRITE_REG(NOC0_REGS_START_ADDR+0x100, oldval);

    //FFB2_0104 - set bit 0 (NOC0 router clkgt en)
    oldval = NOC_READ_REG(NOC0_REGS_START_ADDR+0x104);
    oldval = (oldval&0xFFFFFF00) | 1 | (hyst_val <<1);
    NOC_WRITE_REG(NOC0_REGS_START_ADDR+0x104, oldval);

    //FFB3_0100 - set bit 0 (NOC1 NIU clkgt en)
    oldval = NOC_READ_REG(NOC1_REGS_START_ADDR+0x100);
    oldval = (oldval&0xFFFFFF00) | 1 | (hyst_val <<1);
    NOC_WRITE_REG(NOC1_REGS_START_ADDR+0x100, oldval);

    //FFB3_0104 - set bit 0 (NOC1 router clkgt en)
    oldval = NOC_READ_REG(NOC1_REGS_START_ADDR+0x104);
    oldval = (oldval&0xFFFFFF00) | 1 | (hyst_val <<1);
    NOC_WRITE_REG(NOC1_REGS_START_ADDR+0x104, oldval);

  }
  
}

void set_trisc_address(){

  volatile uint32_t* cfg_regs = core.cfg_regs_base(0);

#ifdef RISC_BLACKHOLE_HW
  WRITE_REG(RISCV_DEBUG_REG_NCRISC_RESET_PC, l1_mem::address_map::NCRISC_HAS_IRAM ? l1_mem::address_map::NCRISC_IRAM_MEM_BASE : l1_mem::address_map::NCRISC_FIRMWARE_BASE); \
  WRITE_REG(RISCV_DEBUG_REG_TRISC0_RESET_PC, l1_mem::address_map::TRISC_BASE);
  WRITE_REG(RISCV_DEBUG_REG_TRISC1_RESET_PC, l1_mem::address_map::TRISC_BASE + l1_mem::address_map::TRISC0_SIZE);
  WRITE_REG(RISCV_DEBUG_REG_TRISC2_RESET_PC, l1_mem::address_map::TRISC_BASE + l1_mem::address_map::TRISC0_SIZE + l1_mem::address_map::TRISC1_SIZE);
  WRITE_REG(RISCV_DEBUG_REG_TRISC_RESET_PC_OVERRIDE, 0b111);
  WRITE_REG(RISCV_DEBUG_REG_NCRISC_RESET_PC_OVERRIDE, 0x1);
#else
  //cfg_regs[NCRISC_RESET_PC_PC_ADDR32] = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
  cfg_regs[NCRISC_RESET_PC_PC_ADDR32] = l1_mem::address_map::NCRISC_HAS_IRAM ? l1_mem::address_map::NCRISC_IRAM_MEM_BASE : l1_mem::address_map::NCRISC_FIRMWARE_BASE ;
  cfg_regs[TRISC_RESET_PC_SEC0_PC_ADDR32] = l1_mem::address_map::TRISC_BASE;
  cfg_regs[TRISC_RESET_PC_SEC1_PC_ADDR32] = l1_mem::address_map::TRISC_BASE + l1_mem::address_map::TRISC0_SIZE;
  cfg_regs[TRISC_RESET_PC_SEC2_PC_ADDR32] = l1_mem::address_map::TRISC_BASE + l1_mem::address_map::TRISC0_SIZE + l1_mem::address_map::TRISC1_SIZE;
  cfg_regs[TRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en_ADDR32] = 0b111;
  cfg_regs[NCRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en_ADDR32] = 0x1;
#endif
}

// Brisc implements risc_reset_vector since Brisc will reset Ncrisc.
void set_risc_reset_vector(){

  volatile uint32_t* cfg_regs = core.cfg_regs_base(0);
  volatile uint32_t *risc_reset_req = (volatile uint32_t *)l1_mem::address_map::NCRISC_L1_CONTEXT_BASE;

  //Address of ncrisc context restore routine.
  //Upon exiting reset, we need to restore ncrisc context so that it can resume program execution.
  //ncrisc puts the address of context restore routine @ 0x5024.
#ifdef RISC_BLACKHOLE_HW
  WRITE_REG(RISCV_DEBUG_REG_NCRISC_RESET_PC, risc_reset_req[1]);
  WRITE_REG(RISCV_DEBUG_REG_NCRISC_RESET_PC_OVERRIDE, 0x1);
#else
  cfg_regs[NCRISC_RESET_PC_PC_ADDR32] = risc_reset_req[1];
  cfg_regs[NCRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en_ADDR32] = 0x1;
#endif
}

void l1_to_ncrisc_iram_copy() {
  if (!l1_mem::address_map::NCRISC_HAS_IRAM) return;
  //Clear flags that brisc will poll on later.
  //This is needed as brisc may start polling these
  //before ncrisc has loaded the epoch.
  RISC_EPOCH_INFO_PTR->all_streams_ready = 0;
  RISC_EPOCH_INFO_PTR->end_program = 0;
  // Copy NCRISC firmware from L1 to local IRAM using tensix DMA
  // NOTE: NCRISC_L1_SCRATCH_BASE (part of NCRISC_FIRMWARE_BASE) is used as NCRISC scratch after NCRISC is loaded
  tdma_xmov(TDMA_MOVER0, (l1_mem::address_map::NCRISC_FIRMWARE_BASE)>>4, (0x4<<12), (l1_mem::address_map::NCRISC_IRAM_CODE_SIZE)>>4, XMOV_L1_TO_L0);
  // Wait for DMA to finish  
  wait_tdma_movers_done(RISCV_TDMA_STATUS_FLAG_MOVER0_BUSY_MASK);
}

// Set up prng seed for stochastic rounding
void set_tensix_prng_seed() {
    #ifdef RISC_B0_HW
    const uint32_t NOC_ID_MASK = (1 << NOC_ADDR_NODE_ID_BITS) - 1;
    uint32_t noc_id = noc_local_node_id() & 0xFFF;
    uint32_t noc_id_x = noc_id & NOC_ID_MASK;
    uint32_t noc_id_y = (noc_id >> NOC_ADDR_NODE_ID_BITS) & NOC_ID_MASK;
    volatile uint32_t* cfg_regs = core.cfg_regs_base(0);
    cfg_regs[PRNG_SEED_Seed_Val_ADDR32] = (noc_id_y << 16) | noc_id_x;
    #endif
}

void device_setup() {

  instrn_buf[0] = core.instrn_buf_base(0);
  instrn_buf[1] = core.instrn_buf_base(1);
  instrn_buf[2] = core.instrn_buf_base(2);

  pc_buf[0] = core.pc_buf_base(0);
  pc_buf[1] = core.pc_buf_base(1);
  pc_buf[2] = core.pc_buf_base(2);

  mailbox[0] = core.mailbox_base(0);
  mailbox[1] = core.mailbox_base(1);
  mailbox[2] = core.mailbox_base(2);

  volatile uint32_t* cfg_regs = core.cfg_regs_base(0);

  stagger_startup();

  core.write_stream_register(0, STREAM_PERF_CONFIG_REG_INDEX,
                           pack_field( 0, CLOCK_GATING_EN_WIDTH, CLOCK_GATING_EN) |
                           pack_field( 2, CLOCK_GATING_HYST_WIDTH, CLOCK_GATING_HYST) |
                           // XXX: This is a performance optimization for relay streams, not power management related
                           pack_field( 32, PARTIAL_SEND_WORDS_THR_WIDTH, PARTIAL_SEND_WORDS_THR));

  // FIXME MT: enable later
  // enable_power_management();

  #ifdef RISC_BLACKHOLE_HW
    // Disable DEST CG 
    *((uint32_t volatile *)RISCV_DEBUG_REG_DEST_CG_CTRL) = 0;
  #endif

  #ifdef RISC_A0_HW
    disable_thcon_clock_gating();
  #endif

  noc_set_active_instance(0);
  uint32_t niu_cfg0 = noc_get_cfg_reg(NIU_CFG_0);
  noc_set_cfg_reg(NIU_CFG_0, niu_cfg0 | 0x1);
  uint32_t router_cfg0 = noc_get_cfg_reg(ROUTER_CFG_0);
  noc_set_cfg_reg(ROUTER_CFG_0, router_cfg0 | 0x1);

  noc_set_active_instance(1);
  uint32_t niu_cfg1 = noc_get_cfg_reg(NIU_CFG_0);
  noc_set_cfg_reg(NIU_CFG_0, niu_cfg1 | 0x1);
  uint32_t router_cfg1 = noc_get_cfg_reg(ROUTER_CFG_0);
  noc_set_cfg_reg(ROUTER_CFG_0, router_cfg1 | 0x1);
  noc_set_active_instance(0);

  set_trisc_address();

  l1_to_ncrisc_iram_copy();


  // Bring NCRISC out of reset, keep TRISCs under reset
  WRITE_REG(RISCV_DEBUG_REG_SOFT_RESET_0, 0x7000);

  // Invalidate tensix icache for all 4 risc cores
  cfg_regs[RISCV_IC_INVALIDATE_InvalidateAll_ADDR32] = 0xf;

  // Clear destination registers
  core.ex_zeroacc(instrn_buf[0]);
  
  // Enable CC stack
  core.ex_encc(instrn_buf[0]);

  // Set default sfpu constant register state
  core.ex_load_const(instrn_buf[0]);

  // Enable ECC scrubber
  core.ex_rmw_cfg(0, ECC_SCRUBBER_Enable_RMW, 1);
  core.ex_rmw_cfg(0, ECC_SCRUBBER_Scrub_On_Error_RMW, 1);
  core.ex_rmw_cfg(0, ECC_SCRUBBER_Delay_RMW, 0x100);

  // Initialize sempahores - check if we need to do this still
	// math->packer semaphore - max set to 1, as double-buffering is disabled by default
  core.ex_sem_init(ckernel::semaphore::MATH_PACK, 1, 0, instrn_buf[0]);
  #ifdef RISC_B0_HW
  core.ex_sem_init(ckernel::semaphore::UNPACK_TO_DEST, 1, 0, instrn_buf[0]);
  core.ex_sem_init(ckernel::semaphore::MATH_DONE, 1, 0, instrn_buf[0]);
  #endif

  set_tensix_prng_seed();

  // // unpacker semaphore
  // core.ex_sem_init(semaphore::UNPACK_MISC, 1, 1, instrn_buf[0]);

  // // unpacker sync semaphore
  // core.ex_sem_init(semaphore::UNPACK_SYNC, 2, 0, instrn_buf[0]);

  // // config state semaphore
  // core.ex_sem_init(semaphore::CFG_STATE_BUSY, MAX_CONFIG_STATES, 0, instrn_buf[0]);


  // Check before clearing debug mailboxes below
  // bool debugger_en = debugger::is_enabled();

  // Initialize debug mailbox to 0s
  for (int i = 0; i < DEBUG_MAILBOX_SIZE; i++)
      core.debug_mailbox()[i] = 0;

}

void init_brisc_streams() {
  num_active_streams = RISC_EPOCH_INFO_PTR->num_active_streams;

  active_stream_info_t* curr_active_stream_info = active_stream_info;
  for (uint32_t i = 0;
       i < num_active_streams;
       i++, curr_active_stream_info++) {
    volatile epoch_stream_info_t tt_l1_ptr * l1_stream_info = RISC_EPOCH_INFO_PTR->active_streams[i];
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
  }
}

int main() {

  disable_lowcache();

  RISC_POST_STATUS(0x10000000);
  // Read counter at start
  risc_init();
  device_setup();


  int epoch_id = 0;

#ifdef PERF_DUMP
  perf::setup_perf_trace();
#endif

  while (true) {
    
    risc_get_next_epoch();

#ifdef PERF_DUMP
    perf::initialize_perf_trace_for_epoch();
    perf::record_timestamp_64b(uint32_t(perf::BriscEventType::EPOCH));
#endif

    if (RISC_EPOCH_INFO_PTR->end_program == 1) {
      RISC_POST_STATUS(0x22000000 | (epoch_id&0xff));
      break;
    }

    init_brisc_streams();
    risc_unpack_pack_stream_handler_init(
       num_input_streams, input_stream_state,
       num_output_streams, output_stream_state
    );
    risc_unpack_pack_stream_handler_loop(
       num_input_streams, input_stream_state,
       num_output_streams, output_stream_state,
       num_active_streams, active_stream_info
    );

#ifdef PERF_DUMP
    perf::record_timestamp_64b(uint32_t(perf::BriscEventType::EPOCH));
    perf::record_perf_dump_end();
    perf::last_trisc_perf_dump_to_dram();
#endif
    // FIXME imatosevic: other sync events: triscs done, anything else? 
    risc_signal_epoch_done();
    RISC_POST_STATUS(0x21000000 | (epoch_id&0xff));
  }

  volatile uint32_t * tt_l1_ptr test_mailbox_ptr = (volatile uint32_t tt_l1_ptr *)(l1_mem::address_map::FIRMWARE_BASE + TEST_MAILBOX_ADDRESS);
  if (test_mailbox_ptr[0] != RISC_DETECTED_STREAM_ASSERT)
    test_mailbox_ptr[0] = 0x1;
  
  while (true)
  {
    risc_reset_check();
  }
  return 0;
}
