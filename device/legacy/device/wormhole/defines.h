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
#define BB_DEBUG_SAMPLING_PERIOD     (5000)
#ifndef ARC_CSM_START_ADDR
#define ARC_CSM_START_ADDR           (0x10000000)
#endif

#define CSM_DUMP_START_ADDR          (ARC_CSM_START_ADDR + (0x20000))                  // This should only be used when and only when 1 ARC core is being used
#define TTKMD_ARC_IF_START_ADDR      (ARC_CSM_START_ADDR + (0x77000))
#define CSM_DUMP_SIZE                (TTKMD_ARC_IF_START_ADDR - CSM_DUMP_START_ADDR)   // bytes
#define MCORE_MB_START_ADDR          (ARC_CSM_START_ADDR + (512*1024-32*1024))
#define NUM_DRAM_INSTANCE            (6)
#define ARC_MAILBOX_WORD_COUNT       64
#define NUM_ACTIVE_TENSIX_CORES      (16)                                              // We'll be getting data from 16 tensix cores
#define PWR_SAMPLES_COUNT            (32)
#define TS_MAX_COUNT                 6
#define PMON_COUNT                   (20)
#define ARC_AXI_LOW_VOLTAGE          700
#define ARC_AXI_MED_VOLTAGE          750
#define ARC_AXI_HIGH_VOLTAGE         900
#define WATCHDOG_FW_CORE             3                                                 // ARC core to run watchdog FW on
#define WATCHDOG_IGNORE_CODE         (0xBEEEDEAF)
#define THROTTLER_TIME_CONSTANT      0.001                                             // changed from 4ms to 1ms base on charz data
#define ERROR_RATE_P                 0.2
#define ERROR_RATE_COEFF_D           0

typedef enum {
  ARC_AXI_PPM_LOW   = 0x0,
  ARC_AXI_PPM_MED   = 0x1,
  ARC_AXI_PPM_HIGH  = 0x2,
} arc_axi_ppm_t;

typedef enum {
  NEBULA_CB  = 0x1,
  GALAXY     = 0x2,
} board_id_t;

typedef enum {
  NO_REV_ID  = 0x0,
  REV2       = 0x2,
  REV3       = 0x3,
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
  FEATURE_PCIE_ACPI_EN,
  FEATURE_REG_HOT_EN,
  FEATURE_PWR_BRAKE_EN,
  FEATURE_BB_DEBUG_EN,
  FEATURE_COUNT,
} featurelist_id_t;

typedef enum {
  DRAM_TRAINING_FAIL,
  DRAM_TRAINING_PASS,
  DRAM_TRAINING_SKIP,
  DRAM_PHY_OFF,
  DRAM_READ_EYE,
} dram_status_e;

typedef enum {
  GDDR_SPEED_16G,
  GDDR_SPEED_14G,
  GDDR_SPEED_12G,
  GDDR_SPEED_10G,
  GDDR_SPEED_8G,
  GDDR_SPEED_COUNT,
} dram_speed_e;

typedef enum {
  GDDR_ERROR_WRITE_TRAINING,   // bit 0 shift
  GDDR_ERROR_READ_TRAINING,    // bit 1 shift
  GDDR_ERROR_ADDR_DECODE,
  GDDR_ERROR_BIT_DECODE,
  GDDR_ERROR_UNKNOWN_SPEED,
  GDDR_ERROR_SETUP_TLB,
  GDDR_ERROR_COUNT,            // max error code = 63
} dram_error_code_e;

typedef enum {
  GDDR_STATUS_TRAINING_NONE,
  GDDR_STATUS_TRAINING_STARTED,
  GDDR_STATUS_TLB_INIT_DONE,
  GDDR_STATUS_PLL_LOCK_DONE,
  GDDR_STATUS_DRAM_CONFIG_DONE,
  GDDR_STATUS_PHY_INIT_DONE,
  GDDR_STATUS_DRAM_CHA_INIT_DONE,
  GDDR_STATUS_DRAM_CHB_INIT_DONE,
  GDDR_STATUS_DRAM_INIT_DONE,
  GDDR_STATUS_CA_TRAINING_DONE,
  GDDR_STATUS_WCK2CK_TRAINING_DONE,
  GDDR_STATUS_LDFF_TRAINING_DONE,
  GDDR_STATUS_READ_TRAINING_DONE,
  GDDR_STATUS_WRITE_TRAINING_DONE,
  GDDR_STATUS_ALL_TRAINING_DONE,
  GDDR_STATUS_INIT_DONE,
  GDDR_STATUS_READ_RETRAIN_STARTED,
  GDDR_STATUS_READ_RETRAIN_DONE,
  GDDR_STATUS_WRITE_RETRAIN_STARTED,
  GDDR_STATUS_WRITE_RETRAIN_DONE,
  GDDR_STATUS_PHY_OFF_STARTED,
  GDDR_STATUS_PHY_OFF_DONE,
  GDDR_STATUS_PHY_PG_ENTER_STARTED,
  GDDR_STATUS_PHY_PG_ENTER_DONE,
  GDDR_STATUS_PHY_PG_RESUME_STARTED,
  GDDR_STATUS_PHY_PG_RESUME_DONE,
  GDDR_STATUS_READ_EYE_STARTED,
  GDDR_STATUS_READ_EYE_DONE,
} dram_post_code_bit_e;

typedef enum {
  ARB_ASIC,             // Specific's ASCI's max frequency - Hardcoded for now, should be from fuses
  ARB_BUSY,             // Used to statically switch to IDLE state
  ARB_TDP,              // Thermal Design Power
  ARB_TDC,              // Thermal Design Current
  ARB_THM,              // Thermal Throttle
  ARB_BUS_PEAK,         // 12V Peak current
  ARB_PWR_BRAKE,        // Power Brake Event
  ARB_EXT_PANIC,        // EDC or Power Brake Event
  ARB_INFRASTRUCTURE,   // PCIE driver specified limit
  ARB_SMBUS,            // Server specified limit - unused
  ARB_REG_HOT,          // Regulator Hot Event
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
  MSG_TYPE_END_DMA_DUMP                    = 0xA7,
  MSG_TYPE_SET_0P8_GDDRVDD_VOLTAGE         = 0x1E,
  MSG_TYPE_SET_1P0_VOLTAGE                 = 0x1F,
  MSG_TYPE_FEATURE_ENABLE_BIT              = 0x20,
  MSG_TYPE_FEATURE_DISABLE_BIT             = 0x21,
  MSG_TYPE_READ_CSM_ADDR                   = 0x22,
  MSG_TYPE_WRITE_CSM_ADDR                  = 0x23,
  MSG_TYPE_WRITE_CSM_DATA                  = 0x24,
  MSG_TYPE_SET_1P35_MVDDQ_VOLTAGE          = 0x25,
  MSG_TYPE_SET_1P8_VOLTAGE                 = 0x26,
  MSG_TYPE_GET_FREQ_CURVE_FROM_VOLTAGE     = 0x30,
  MSG_TYPE_AISWEEP_START                   = 0x31,
  MSG_TYPE_AISWEEP_STOP                    = 0x32,
  MSG_TYPE_FORCE_AICLK                     = 0x33,
  MSG_TYPE_GET_AICLK                       = 0x34,
  MSG_TYPE_AICLK_PPM_EN                    = 0x35,
  MSG_TYPE_AI_BOOST_EN                     = 0x36,
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
  MSG_TYPE_ENABLE_ETH_QUEUE                = 0x58,
  MSG_TYPE_INIT_PLL                        = 0x65,
  MSG_TYPE_CLOCK_BYPASS_ENTER_27MHz        = 0x66,
  MSG_TYPE_CLOCK_BYPASS_EXIT_27MHz         = 0x77,
  MSG_TYPE_RUN_FAST_CLOCKS                 = 0x88,
  MSG_TYPE_TEST                            = 0x90,
  MSG_TYPE_SETUP_IATU_FOR_PEER_TO_PEER     = 0x97,
  MSG_TYPE_DBI_WRITE                       = 0x98,
  MSG_TYPE_PCIE_MSI_TEST                   = 0x99,
  MSG_TYPE_NOC_DMA_TRANSFER                = 0x9A,
  MSG_TYPE_PCIE_DMA_CHIP_TO_HOST_TRANSFER  = 0x9B,
  MSG_TYPE_PCIE_DMA_HOST_TO_CHIP_TRANSFER  = 0x9C,
  MSG_TYPE_PCIE_ERROR_CNT_RESET            = 0x9D,
  MSG_TYPE_PCIE_MUTEX_ACQUIRE              = 0x9E,
  MSG_TYPE_TRIGGER_IRQ                     = 0x9F,
  MSG_TYPE_ARC_STATE0                      = 0xA0,
  MSG_TYPE_ARC_STATE1                      = 0xA1,
  MSG_TYPE_ARC_STATE3                      = 0xA3,
  MSG_TYPE_ARC_STATE5                      = 0xA5,
  MSG_TYPE_GET_VOLTAGE_CURVE_FROM_FREQ     = 0xA6,
  MSG_TYPE_DRAM_WRITE_RETRAIN              = 0xA8,
  MSG_TYPE_DRAM_READ_RETRAIN               = 0xA9,
  MSG_TYPE_DRAM_PHY_OFF                    = 0xAA,   // used for harvesting
  MSG_TYPE_DRAM_PHY_PG_ENTER               = 0xAB,
  MSG_TYPE_DRAM_PHY_PG_RESUME              = 0xAC,
  MSG_TYPE_GET_DRAM_TEMPERATURE            = 0xAD,
} MSG_TYPE;

