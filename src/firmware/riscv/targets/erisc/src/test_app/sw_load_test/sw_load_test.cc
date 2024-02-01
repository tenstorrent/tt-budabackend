#include "eth_init.h"
#include "overlay.h"
#include "noc.h"
#include "risc.h"
#include "noc_nonblocking_api.h"
#include "eth_routing_v2.h"
#include "eth_ss.h"
#include "test_app_common.h"
#include <cstdint>
#include "eth_l1_address_map.h"

#define ETH_APP_VERSION_MAJOR  0x1
#define ETH_APP_VERSION_MINOR  0x0
#define ETH_APP_VERSION_TEST   0x0

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

void ApplicationHandler(void)
{
  init_rtos_table();

  ncrisc_noc_counters_init();
  debug_buf[97] = (ETH_APP_VERSION_MAJOR << 16) | (ETH_APP_VERSION_MINOR << 8) | ETH_APP_VERSION_TEST;
  // if (config_buf->run_test_app) {
  //   run_sw_load_test();
  // }
}
