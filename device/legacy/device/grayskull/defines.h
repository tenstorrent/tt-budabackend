// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// *************************************** //
//      THIS FILE IS AUTO GENERATED        //
// *************************************** //

#pragma once

#define SAMPLING_PERIOD_IN_US        (1000)
#define TMON_SAMPLING_PERIOD         (10)
#define PWR_REPORT_SAMPLING_PERIOD   (1)
#define PMON_SAMPLING_PERIOD         (1)
#define DEBUG_SAMPLING_PERIOD        (1)
#define PPM_SAMPLING_PERIOD          (1)
#define ACPI_DSTATE_SAMPLING_PERIOD  (1)
#ifndef ARC_CSM_START_ADDR
#define ARC_CSM_START_ADDR           (0x10000000)
#endif

#define CSM_DUMP_START_ADDR          (ARC_CSM_START_ADDR + (0xF000))               // This should only be used when and only when 1 ARC core is being used
#define MCORE_MB_START_ADDR          (ARC_CSM_START_ADDR + (512*1024-32*1024))
#define CSM_DUMP_SIZE                (MCORE_MB_START_ADDR - CSM_DUMP_START_ADDR)   // bytes
#define NUM_DRAM_INSTANCE            (6)
#define ARC_MAILBOX_WORD_COUNT       64
#define NUM_ACTIVE_TENSIX_CORES      (16)                                          // We'll be getting data from 16 tensix cores
#define PWR_SAMPLES_COUNT            (32)
#define TS_MAX_COUNT                 6
#define PMON_COUNT                   (25)
#define ARC_AXI_LOW_VOLTAGE          700
#define ARC_AXI_MED_VOLTAGE          750
#define ARC_AXI_HIGH_VOLTAGE         900
#define WATCHDOG_IGNORE_CODE         (0xBEEEDEAF)
#define THROTTLER_TIME_CONSTANT      0.001                                         // changed from 4ms to 1ms base on charz data
#define ERROR_RATE_P                 0.2
#define ERROR_RATE_COEFF_D           0

typedef enum {
  ARC_AXI_PPM_LOW   = 0x0,
  ARC_AXI_PPM_MED   = 0x1,
  ARC_AXI_PPM_HIGH  = 0x2,
} arc_axi_ppm_t;

typedef enum {
  NEBULA_CB  = 0x1,
} board_id_t;

typedef enum {
  REV2  = 0x2,
  REV3  = 0x3,
} boardrev_id_t;

typedef enum {
  FEATURE_PMON,
  FEATURE_TMON,
  FEATURE_FANCTRL,
  FEATURE_TELEMETRY,
  FEATURE_DEBUG,
  FEATURE_AICLK_PPM,
  FEATURE_TDP_PPM,
  FEATURE_TDC_PPM,
  FEATURE_THM_PPM,
  FEATURE_DMA_DUMP,
  FEATURE_BUS_PEAK_PPM,
  FEATURE_COUNT,
} featurelist_id_t;

typedef enum {
  DRAM_FEATURE_DBI,
  DRAM_FEATURE_AUTO_SELF_REFRESH,
  DRAM_FEATURE_EDC_TRACKING,
  DRAM_FEATURE_SOFTWARE_READ_TRAINING,
  DRAM_FEATURE_SOFTWARE_WRITE_TRAINING,
  DRAM_FEATURE_TRAINING_DATA_BUFFER,
  DRAM_FEATURE_ECC,
  DRAM_FEATURE_ZQ_CALIB,
  DRAM_FEATURE_COUNT,
} dram_feature_list_id_t;

typedef enum {
  ARB_ASIC,             // Specific's ASCI's max frequency - Hardcoded for now, should be from fuses
  ARB_BUSY,             // Used to statically switch to IDLE state
  ARB_TDP,              // Thermal Design Power
  ARB_TDC,              // Thermal Design Current
  ARB_THM,              // Thermal Throttle
  ARB_BUS_PEAK,         // 12V Peak current
  ARB_EXT_PANIC,        // EDC or Power Brake Event
  ARB_INFRASTRUCTURE,   // PCIE driver specified limit
  ARB_SMBUS,            // Server specified limit - unused
  ARB_COUNT,
} arbiterlist_id_t;

typedef enum {
  TMON0,
  TMON1,
  TMON2,
  TMON3,
  TMON4,
  TMON5,
  PMON0,
  PMON1,
  PMON2,
  PMON3,
  PMON4,
  PMON5,
  PMON6,
  PMON7,
  PMON8,
  PMON9,
  PMON10,
  PMON11,
  PMON12,
  PMON13,
  PMON14,
  PMON15,
  PMON16,
  PMON17,
  PMON18,
  PMON19,
  PMON20,
  PMON21,
  PMON22,
  PMON23,
  PMON24,
  PMON25,
  PMON26,
  PMON27,
  PMON28,
  PMON29,
} mon_id_t;

// ███╗   ███╗███████╗███████╗███████╗ █████╗  ██████╗ ███████╗
// ████╗ ████║██╔════╝██╔════╝██╔════╝██╔══██╗██╔════╝ ██╔════╝
// ██╔████╔██║█████╗  ███████╗███████╗███████║██║  ███╗█████╗
// ██║╚██╔╝██║██╔══╝  ╚════██║╚════██║██╔══██║██║   ██║██╔══╝
// ██║ ╚═╝ ██║███████╗███████║███████║██║  ██║╚██████╔╝███████╗
// ╚═╝     ╚═╝╚══════╝╚══════╝╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝
typedef enum {
  MSG_TYPE_NOP                             = 0x11,   // Do nothing
  MSG_TYPE_SET_VOLTAGE                     = 0x12,
  MSG_TYPE_DDR_TRAIN                       = 0x13,
  MSG_TYPE_DDR_DMA_TEST                    = 0x14,
  MSG_TYPE_TELEMETRY_START                 = 0x15,
  MSG_TYPE_TELEMETRY_STOP                  = 0x16,
  MSG_TYPE_GET_TEMPERATURE                 = 0x17,
  MSG_TYPE_GET_PMON_FAIL_COUNT             = 0x18,
  MSG_TYPE_PMON_DUMP                       = 0x19,
  MSG_TYPE_TELEMETRY_DUMP                  = 0x1A,
  MSG_TYPE_SET_VDDIO_VOLTAGE               = 0x1B,
  MSG_TYPE_SET_PMON_RO_SEL                 = 0x1C,
  MSG_TYPE_FEATURE_ENABLE_BIT              = 0x20,
  MSG_TYPE_FEATURE_DISABLE_BIT             = 0x21,
  MSG_TYPE_READ_CSM_ADDR                   = 0x22,
  MSG_TYPE_WRITE_CSM_ADDR                  = 0x23,
  MSG_TYPE_WRITE_CSM_DATA                  = 0x24,
  MSG_TYPE_AISWEEP_START                   = 0x31,
  MSG_TYPE_AISWEEP_STOP                    = 0x32,
  MSG_TYPE_FORCE_AICLK                     = 0x33,
  MSG_TYPE_GET_AICLK                       = 0x34,
  MSG_TYPE_AICLK_PPM_EN                    = 0x35,
  MSG_TYPE_EXT_PANIC_ASSERT                = 0x36,
  MSG_TYPE_EXT_PANIC_DEASSERT              = 0x37,
  MSG_TYPE_SW_AICLK_FMAX                   = 0x38,
  MSG_TYPE_FORCE_VDD                       = 0x39,
  MSG_TYPE_RESNET50_DEMO_START             = 0x40,
  MSG_TYPE_RESNET50_DEMO_TEST              = 0x42,
  MSG_TYPE_RESNET50_DEMO_STOP              = 0x45,
  MSG_TYPE_ARC_GO_BUSY                     = 0x52,
  MSG_TYPE_ARC_GO_SHORT_IDLE               = 0x53,
  MSG_TYPE_ARC_GO_LONG_IDLE                = 0x54,
  MSG_TYPE_ARC_GO_TO_SLEEP                 = 0x55,
  MSG_TYPE_ARC_GET_HARVESTING              = 0x57,
  MSG_TYPE_INIT_PLL                        = 0x65,
  MSG_TYPE_CLOCK_BYPASS_ENTER_27MHz        = 0x66,
  MSG_TYPE_CLOCK_BYPASS_EXIT_27MHz         = 0x77,
  MSG_TYPE_RUN_FAST_CLOCKS                 = 0x88,
  MSG_TYPE_TEST                            = 0x90,
  MSG_TYPE_PCIE_MSI_TEST                   = 0x99,
  MSG_TYPE_NOC_DMA_TRANSFER                = 0x9A,
  MSG_TYPE_PCIE_DMA_CHIP_TO_HOST_TRANSFER  = 0x9B,
  MSG_TYPE_PCIE_DMA_HOST_TO_CHIP_TRANSFER  = 0x9C,
  MSG_TYPE_END_DMA_DUMP                    = 0xA1,
} MSG_TYPE;

