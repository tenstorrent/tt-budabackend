
#include "version.h"
#include "risc.h"
#include "noc_nonblocking_api.h"
#include "eth_init.h"
#include "eth_routing_v2.h"
#include "rtos.h"

// NOTE: Last USED test_results index: 49
// Please update this when you place more data into test_results to avoid conflict

uint32_t FW_VERSION[4] __attribute__((section(".fw_version"))) =
    {FW_VERSION_SEMANTIC, FW_VERSION_DATE, FW_VERSION_LOW, FW_VERSION_HIGH};

extern uint32_t FWEVENT_MAILBOX;

// extern storage from noc_nonblocking.h
uint32_t noc_reads_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_num_issued[NUM_NOCS];
uint32_t noc_nonposted_writes_acked[NUM_NOCS];
uint32_t noc_xy_local_addr[NUM_NOCS];

volatile boot_params_t* const config_buf = (volatile boot_params_t*)(BOOT_PARAMS_ADDR);
volatile uint32_t* const test_results = (volatile uint32_t*)(RESULTS_BUF_ADDR);

int main(int argc, char *argv[]) {

  //Write firmware version to register that software can read back.
  RISC_POST_FW_VERSION(FW_VERSION[0]);
  //RISC_POST_STATUS(0x10000000);

  ncrisc_noc_init();

  RISC_POST_STATUS(0x10000001);

  rtosAddTask((void *)ApplicationTask, STACK_TOP, TASK_1_INDEX, 1);
  //rtosAddTask((void *)Task2, STACK_TOP-STACK_SIZE, TASK_2_INDEX, 1);
  rtosAddTask((void *)0, 0, MAIN_TASK_INDEX, 1);

  uint32_t i = 0;
  eth_routing_init();
  volatile uint32_t *post_code = (volatile uint32_t *) 0xffb3010c;

  do {
      uint32_t heartbeat;
      heartbeat = 0xABCD0000 + i;
      FWEVENT_MAILBOX = heartbeat;
      test_results[48] = heartbeat; // Helps prevent A0 NOC arbiter issue for heartbeat register read
      i++;
      i &= 0xFFFF;
      *post_code = 0x02010000;
      eth_routing_handler();
      *post_code = 0x02020000;
      eth_link_status_check();
      *post_code = 0x02030000;

      ncrisc_noc_full_sync();
      *post_code = 0x02040000;
      rtos_context_switch();
      *post_code = 0x02050000;
      ncrisc_noc_counters_init();

  } while (true);

  while (true);
  return 0;
}
