// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// *************************************** //
//      THIS FILE IS AUTO GENERATED        //
// *************************************** //

#pragma once

#include <stdint.h>
#include "defines.h"

typedef struct {
  uint16_t  pmon_val[PMON_COUNT];
  uint16_t  pmon_cal[PMON_COUNT];
  uint32_t  pmon_fail_count_l1;
  uint32_t  pmon_fail_count_l2;
  uint16_t  pmon_max;
  uint16_t  pmon_min;
  uint32_t  pmon_avg;                          // stored as 4 bit percision
  uint32_t  pmon_calavg;                       // stored as 4 bit percision
  uint16_t  pmon_l2_under_per[PMON_COUNT];
  uint16_t  pmon_l2_over_per[PMON_COUNT];
  uint32_t  pmon_avg_bin[9];
  uint8_t   pmon_ro_sel;
  uint8_t   spare1[3];
  uint32_t  spare2[64];
} pmon_t; // 129 * 4 = 516B

typedef struct {
  uint16_t  ts[TS_MAX_COUNT];
  uint8_t   ts_fail_count[TS_MAX_COUNT];
  uint8_t   ts_efuse_invalid;
  uint8_t   ts_mode;
  uint8_t   spare[4];
  uint8_t   ts_caldone;
  uint8_t   ts_using_efuse;
  uint8_t   ts_total;
  uint8_t   ts_valid;
  uint32_t  ts_avg;                          // Being stored as 4 bit percision
  uint32_t  ts_lta;                          // Being stored as 4 bit percision because the rounding get lots and LTA value is always lower than AVG by several degree
  uint32_t  ts_max;
} tmon_t; // 10 * 4 = 40B

typedef struct {
  uint32_t  uint_voltage;
  uint32_t  uint_current;
  uint32_t  board_id;
  uint32_t  boardrev_id;
  float     voltage;
  float     current;
  float     power;
  uint32_t  count;
  float     min_current;
  float     max_current;
  float     sum_current;
  float     mean_current;
  float     min_power;
  float     max_power;
  float     sum_power;
  float     mean_power;
  float     power_samples[PWR_SAMPLES_COUNT];
  uint32_t  reserved_1[16];
} pwr_rpt_mb_t; // 64 * 4 = 256B

typedef struct {
  uint32_t  counter;
  uint32_t  image_count_per_ttx;
  uint32_t  cmd;
  uint32_t  image_count;
  uint32_t  loop_ttx;
  uint32_t  out_per_image;
  uint32_t  reset;
  uint32_t  reserved[1];
} resnet50_demo_t; // 8 * 4 = 32B

typedef struct {
  uint16_t  value[NUM_ACTIVE_TENSIX_CORES*5];     // Max 5 values from FC output per core
  uint16_t  index[NUM_ACTIVE_TENSIX_CORES*5];     // Indexes for top 5 values from FC output core
  uint32_t  c0_ready;
  uint32_t  c1_ready;
  uint32_t  c2_ready;
  uint32_t  c3_ready;
} per_core_max_t; // 84 * 4 = 336B

typedef struct {
  uint16_t  training_bit_status;
  uint8_t   error_bit_status;
  uint8_t   channel_status;
} gddr_status_t; // 1 * 4 = 4B

typedef union {
  gddr_status_t  instance;
  uint32_t       val;
} gddr_status_u; // 1 * 4 = 4B

typedef struct {
  uint32_t  dbi_en                      : 1;
  uint32_t  auto_refresh_en             : 1;
  uint32_t  edc_tracking_en             : 1;
  uint32_t  software_read_training_en   : 1;
  uint32_t  software_write_training_en  : 1;
  uint32_t  training_data_buffer_en     : 1;
  uint32_t  ecc_en                      : 1;
  uint32_t  zq_calib_en                 : 1;
  uint32_t  spare                       : 24;
} dram_feature_enable_bit_t; // 1 * 4 = 4B

typedef union {
  dram_feature_enable_bit_t  f;
  uint32_t                   val;
} dram_feature_enable_bit_u; // 1 * 4 = 4B

typedef struct {
  uint32_t  pmon_en          : 1;
  uint32_t  tmon_en          : 1;
  uint32_t  fan_ctrl_en      : 1;
  uint32_t  telemetry_en     : 1;
  uint32_t  debug_en         : 1;
  uint32_t  aiclk_ppm_en     : 1;
  uint32_t  tdp_ppm_en       : 1;
  uint32_t  tdc_ppm_en       : 1;
  uint32_t  thm_ppm_en       : 1;
  uint32_t  dma_dump_en      : 1;
  uint32_t  bus_peak_ppm_en  : 1;
  uint32_t  reserved         : 21;
} feature_enable_bit_t; // 1 * 4 = 4B

typedef union {
  feature_enable_bit_t  f;
  uint32_t              val;
} feature_enable_bit_u; // 1 * 4 = 4B

typedef struct {
  uint32_t  curr_aiclk;
  uint32_t  targ_aiclk;
  uint32_t  force;
  uint32_t  force_aiclk;
  uint32_t  sweep_en;
  uint32_t  sweep_high;
  uint32_t  sweep_low;
  uint32_t  sweep_period;
  uint32_t  sweep_count;
  uint32_t  short_idle;
  uint32_t  curr_vdd;                             // stored as mV
  uint32_t  targ_vdd;                             // stored as mV
  uint16_t  force_vdd_en;
  uint16_t  force_vdd;                            // stored as mv
  uint32_t  arc_pll_init;                         // 0 = infrastructured controled, so no ARC/AXI switching, 1 == ARC FW controlled
  uint32_t  curr_arcaxi_state;                    // 0 = low, 1 == med, 2 == high
  uint32_t  targ_arcaxi_state;                    // 0 = low, 1 == med, 2 == high
  uint32_t  curr_arcclk;
  uint32_t  curr_axiclk;
  uint32_t  aiclk_arb_max[ARB_COUNT];
  uint32_t  aiclk_throttler_count_en;
  uint32_t  aiclk_throttler_total_count;
  uint32_t  aiclk_throttler_sum_freq;
  uint32_t  aiclk_throttler_count[ARB_COUNT];
  uint32_t  bus_3p3_current;
  uint32_t  spare[60-22-ARB_COUNT*2];             // This structure needs to sum up to 60 for now until we remove all hardcoded indexing from the infrastructure specifically related to ARC Mailbox
} aiclk_ppm_t; // 60 * 4 = 240B

typedef struct {
  uint32_t  curr_fan;
  uint32_t  spare[4-1];
} fan_ctrl_ppm_t; // 4 * 4 = 16B

typedef struct {
  float  limit;
  float  value;
  float  alpha;
  float  err_rate_P;
  float  err_rate_coeff_D;
  float  error;
  float  error_prev;
  float  output;
  float  throttle;
} throttler_t; // 9 * 4 = 36B

// SPI Header Mapping
typedef struct {
  uint16_t  mapping_version;         // Initial release is version 1
  uint16_t  reprogrammed_count;      // Keep incrementing this value as the header gets updated every time
  uint32_t  date_programmed;         // Created/Modified
  uint32_t  mapping_table[64];       // Mapping the high level content of the above table
  uint32_t  board_info[2];           // See table corresponding to this
  uint32_t  vrm_checksum;
  uint32_t  rework_id[4];            // Each bit corresponds to a specific rework - this will be mapped per Unique Part Identifier
  uint32_t  asic_id[4];              // ASIC ID (8 ASCII to cover TT/FF/SSxx-xxx) - Will be teporary until everything is fused in
  uint16_t  auto_init;               // Determins if the board is ready to be auto initialized upon OS bootup (1 == will auto initialize)
  uint16_t  asic_fmax;
  uint16_t  vdd_max;                 // Stored as mV
  uint16_t  vdd_min;
  int32_t   voltage_margin;          // Stored as mV as an integer
  uint16_t  tdp_limit;
  uint16_t  tdc_limit;
  uint16_t  thm_limit;
  uint16_t  therm_trip_l1_limit;
  uint16_t  bus_peak_limit;
  uint16_t  dual_rank;
  float     vf_slope;
  float     vf_offset;
} spi_table_t; // 85 * 4 = 340B

typedef union {
  uint32_t     val[sizeof(spi_table_t)/sizeof(uint32_t)];
  spi_table_t  f;
} spi_table_u; // 85 * 4 = 340B

typedef struct {
  uint32_t  heartbeat;
  uint32_t  total_timer_irqs;
  uint32_t  missed_timer_irqs;
  uint32_t  message_pending;
  uint32_t  workq_addr;
  uint32_t  exception_cause;
  uint32_t  exception_fault_address;
  uint32_t  dma_dump_phys_addr;
  uint32_t  dma_dump_chunks;
  uint32_t  dma_dump_words;
  uint32_t  dma_dump_addr_list[32];
} debug_t; // 42 * 4 = 168B

typedef struct {
  uint32_t  Production_FW_Version_high;
  uint32_t  Production_FW_Version_low;
  uint32_t  Watchdog_FW_Version_high;
  uint32_t  Watchdog_FW_Version_low;
} version_t; // 4 * 4 = 16B

typedef struct {
  uint32_t  chip_addr;
  uint32_t  host_phys_addr;
  uint32_t  completion_flag_phys_addr;
  uint32_t  size_bytes                  : 28;
  uint32_t  write                       : 1;
  uint32_t  pcie_msi_on_done            : 1;
  uint32_t  pcie_write_on_done          : 1;
  uint32_t  trigger                     : 1;
  uint32_t  repeat;
} arc_pcie_ctrl_dma_request_t; // 5 * 4 = 20B

typedef struct {
  uint32_t  msi_32b_addr;
  uint32_t  msi_data;
  uint32_t  completion_flag_phys_addr;
} pcie_config_t; // 3 * 4 = 12B

typedef struct {
  pwr_rpt_mb_t                 pwr_rpt;                                 // 256B
  resnet50_demo_t              resnet50_demo;                           // 32B
  per_core_max_t               per_core_max;                            // 336B
  aiclk_ppm_t                  aiclk_ppm;                               // 240B
  fan_ctrl_ppm_t               fan_ctrl_ppm;                            // 16B
  tmon_t                       tmon;                                    // 40B
  gddr_status_u                dram_status[NUM_DRAM_INSTANCE];          // 24B
  uint32_t                     gddr_debug_scratch[5];                   // 20B
  feature_enable_bit_u         feature_enable_bit;                      // 4B
  uint32_t                     ARC_MAILBOX[ARC_MAILBOX_WORD_COUNT];     // 256B
  uint32_t                     ARC_MAILBOX_SIZE;                        // 4B
  arc_pcie_ctrl_dma_request_t  arc_pcie_dma_request;                    // 20B
  pcie_config_t                pcie_config;                             // 12B
  pmon_t                       pmon;                                    // 516B
  debug_t                      debug;                                   // 168B
  uint32_t                     dram_speed;                              // 4B - the speed in Gbps at which we trained DDDR
  version_t                    version;                                 // 16B
  throttler_t                  tdp_ppm;                                 // 36B
  throttler_t                  tdc_ppm;                                 // 36B
  throttler_t                  thm_ppm;                                 // 36B
  throttler_t                  bus_peak_ppm;                            // 36B
  spi_table_u                  spi_table;                               // 340B
  dram_feature_enable_bit_u    dram_feature_enable_bit;                 // 4B
} mcore_mb_t; // 613 * 4 = 2452B

typedef union {
  uint32_t    val[sizeof(mcore_mb_t)/sizeof(uint32_t)];
  mcore_mb_t  f;
} mcore_mb_u; // 613 * 4 = 2452B

