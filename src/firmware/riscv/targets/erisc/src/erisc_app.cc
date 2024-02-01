
#include "noc_overlay_parameters.h"
#include "noc_wrappers.h"
#include "epoch.h"
#include "eth_l1_address_map.h"
#include "stream_interface.h"
#include "risc_epoch.h"
#include "tensix.h"
#include "eth_init.h"
#include "eth_ss.h"
#include "rtos.h"

void (*ApplicationHandlerPtr)(void);

extern uint32_t __erisc_jump_table;

#define INITIAL_EPOCH_VECTOR_TABLE_MAX_X  16
#define EPOCH_Q_SLOT_SIZE 8
#define EPOCH_Q_WRPTR_OFFSET 4
#define EPOCH_Q_RDPTR_OFFSET 0
#define EPOCH_Q_SLOTS_OFFSET 32

#define SCRATCH_PAD_DRAM_READ_IDX 0
#define SCRATCH_PAD_DRAM_WRITE_IDX 16

#define INITIAL_EPOCH_VECTOR_TABLE_ENTRY_SIZE_BYTES (EPOCH_QUEUE_NUM_SLOTS*EPOCH_Q_SLOT_SIZE+EPOCH_Q_SLOTS_OFFSET)
#define INITIAL_EPOCH_VECTOR_TABLE_DRAM_ADDR  (40*1024*1024-INITIAL_EPOCH_VECTOR_TABLE_MAX_X*INITIAL_EPOCH_VECTOR_TABLE_MAX_X*INITIAL_EPOCH_VECTOR_TABLE_ENTRY_SIZE_BYTES)

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
volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);

void __attribute__((section(".misc_init"))) risc_wait_for_epoch_q(void)
{
  while (true)
  {
    my_q_rd_ptr = risc_get_l1_epoch_q_rdptr();
    my_q_wr_ptr = risc_get_l1_epoch_q_wrptr();
    bool poll_dram_epoch_queue = my_q_wr_ptr == epoch_queue::POLLING_EPOCH_QUEUE_TAG;
    if (poll_dram_epoch_queue) {
      risc_epoch_q_get_ptr(noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, my_q_wr_ptr); // read the epoch rd/wr pointers that Host sw will reset to 0 for new epoch command run.
    }
    if (my_q_rd_ptr == 0 && my_q_wr_ptr == 0) {
      break;
    }
    uint32_t start_time = *((volatile uint32_t tt_reg_ptr *)(RISCV_DEBUG_REG_WALL_CLOCK_L));
    uint32_t end_time = start_time;
    while (poll_dram_epoch_queue && end_time-start_time < 100000) // wait ~1ms before reading rd/wr pointers again.
    { 
      rtos_context_switch();
      end_time = *((volatile uint32_t tt_reg_ptr *)(RISCV_DEBUG_REG_WALL_CLOCK_L));
    }
    noc_read_scratch_buf[29] = 0x11220004;
    rtos_context_switch();
  }

}

void __attribute__((section(".misc_init"))) ApplicationTask(void)
{

  uint32_t epoch_q_active_count = 0;
  epoch_empty_check_cnt = 0;
  ApplicationHandlerPtr = (void (*)()) eth_l1_mem::address_map::FIRMWARE_BASE; //Start Address of Application Handler. This is loaded by runtime application.
  uint32_t *RtosTable = (uint32_t *) &__erisc_jump_table;    //Rtos Jump Table. Runtime application needs rtos function handles.
  RtosTable[0] = (uint32_t) &rtos_context_switch; //Put handle to rtos_context_swtich at index 0

  for (uint32_t n = 0; n < NUM_NOCS; n++) {
    uint32_t noc_id_reg = NOC_CMD_BUF_READ_REG(n, 0, NOC_NODE_ID);
    my_x[n] = noc_id_reg & NOC_NODE_ID_MASK;
    my_y[n] = (noc_id_reg >> NOC_ADDR_NODE_ID_BITS) & NOC_NODE_ID_MASK;
    if (n == 0) {
      noc_size_x = (noc_id_reg >> (NOC_ADDR_NODE_ID_BITS+NOC_ADDR_NODE_ID_BITS)) & ((((uint64_t)0x1) << (NOC_ADDR_NODE_ID_BITS+1)) - 1);
      noc_size_y = (noc_id_reg >> (NOC_ADDR_NODE_ID_BITS+NOC_ADDR_NODE_ID_BITS+(NOC_ADDR_NODE_ID_BITS+1))) & ((((uint64_t)0x1) << (NOC_ADDR_NODE_ID_BITS+1)) - 1);
    }

    set_noc_trans_table(n, noc_trans_table_en, my_logical_x[n], my_logical_y[n]);
  }

  loading_noc = my_x[0] % NUM_NOCS;
  my_q_rd_ptr = 0;
  my_q_wr_ptr = 0;

  risc_l1_epoch_q_reset();
  my_q_table_offset = risc_get_epoch_q_dram_ptr(my_x[0], my_y[0]);

  bool epoch_q_empty = true;
  do {
    if (config_buf->dram_trained) {
      epoch_q_empty = risc_is_epoch_q_empty(noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, my_q_wr_ptr, epoch_empty_check_cnt);
    }
    if (!epoch_q_empty || config_buf->run_test_app) {
      epoch_q_active_count++;
      ncrisc_noc_full_sync();
      //Call Application Handler. 
      ApplicationHandlerPtr();
      //read back noc counters as Application noc read/writes
      //were invisible to this task.
      ncrisc_noc_counters_init();
      epoch_empty_check_cnt = 0;
      if (config_buf->run_test_app == 0) {
        risc_wait_for_epoch_q();
      }
    } else {
      RISC_POST_STATUS(0x1BC1DEAD);
      rtos_context_switch();
    }
  } while (true);

  while (true);
}
