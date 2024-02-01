#include "eth_init.h"
//#include "overlay.h"
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

uint32_t noc_xy_local_addr[NUM_NOCS];

/*
void noc_write (uint32_t noc_id, uint32_t src_addr, uint64_t dest_addr, uint32_t size)
{
  while (!ncrisc_noc_fast_write_ok(noc_id, NCRISC_WR_CMD_BUF));
  ncrisc_noc_fast_write(noc_id, NCRISC_WR_CMD_BUF,
                        src_addr,
                        dest_addr,
                        size,
                        0, false, false, 1, NCRISC_WR_DEF_TRID);
  while (!ncrisc_noc_nonposted_writes_flushed(noc_id, NCRISC_WR_DEF_TRID));
}
*/
void ApplicationHandler(void)
{
  RISC_POST_STATUS(0xFACE0001);

  init_rtos_table();
  ncrisc_noc_init();

  ncrisc_noc_counters_init();
  debug_buf[97] = (ETH_APP_VERSION_MAJOR << 16) | (ETH_APP_VERSION_MINOR << 8) | ETH_APP_VERSION_TEST;

  volatile uint32_t tt_l1_ptr * temp = (volatile uint32_t *)0x9030;
  volatile uint64_t tt_l1_ptr * timestamps = (volatile uint64_t *)0x11000;

  uint64_t src_buf_addr;
  uint32_t dest_buf_addr = 0x18000;

  uint64_t start = eth_read_wall_clock();
  uint32_t dest_offset = 10000;
  uint32_t block_size = 1024;
  uint32_t block_count = 16384;
  RISC_POST_STATUS(0xFACE0002);
  timestamps[0] = start;
  src_buf_addr = NOC_XY_ADDR(0, 3, (0x800000000 | temp[0]));
  uint32_t mode = temp[1];
  
  if (temp[3] & 0xFFFF) {
    block_size = temp[3] & 0xFFFF;
  }
  if ((temp[3] >> 16) & 0xFFFF) {
    block_count = temp[3] >> 16;
  }
  
  for (uint32_t i = 0; i < block_count; i++) {
    RISC_POST_STATUS(0xFACE0003);
    timestamps[3] = src_buf_addr;
    RISC_POST_STATUS(0xFACE0013);
    ncrisc_noc_fast_read_any_len(0, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(0, 3, src_buf_addr), dest_buf_addr, block_size, NCRISC_ETH_START_TRID);
    //ncrisc_noc_fast_read(0, NCRISC_SMALL_TXN_CMD_BUF, NOC_XY_ADDR(1, 0, 0x0), dest_buf_addr, block_size, NCRISC_ETH_START_TRID);

    RISC_POST_STATUS(0xFACE0023);
    while (!ncrisc_noc_reads_flushed(0,NCRISC_ETH_START_TRID));
    RISC_POST_STATUS(0xFACE0004);

    if (mode == 1) {
      ncrisc_noc_fast_write_any_len(0, NCRISC_WR_CMD_BUF, dest_buf_addr, NOC_XY_ADDR(0, 0, dest_offset), block_size, 0, false, false, 1, NCRISC_WR_DEF_TRID);
      while (!ncrisc_noc_nonposted_writes_flushed(0, NCRISC_WR_DEF_TRID));
      dest_offset += block_size;
    }
    RISC_POST_STATUS(0xFACE0005);
  }
 
  uint64_t end = eth_read_wall_clock();
  uint64_t diff = end - start;
  
  timestamps[1] = end;
  timestamps[2] = diff;

  while (config_buf->run_test_app == 1) {
    temp[2] = 0xFACEDEAD;
    RISC_POST_STATUS(0xFACEDEAD);
    risc_context_switch();
  }

  temp[2] = 0;
  // if (config_buf->run_test_app) {
  //   run_sw_load_test();
  // }
}
