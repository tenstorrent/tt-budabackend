include $(BUDA_HOME)/infra/common.mk

CKERNELS_DIR=$(BUDA_HOME)/src/ckernels
ARCH_NAME ?= grayskull
DEVICE_TYPE=0

PERF_DUMP ?= 0
INTERMED_DUMP ?= 0
PERF_DUMP_LEVEL ?= 0
DRAM_DECOUPLE ?= 0
PERF_DUMP_CONCURRENT ?= 0
OVERLAY_DECOUPLE ?= 0

DEFINES = 

SOURCES = \
	ncrisc.cc \
	context.cc \
	contextASM.S \
	$(BUDA_HOME)/src/firmware/riscv/common/dram_stream_intf.cc \
	$(BUDA_HOME)/src/firmware/riscv/common/risc_common.cc \
	$(BUDA_HOME)/src/firmware/riscv/common/risc_epoch.cc \


ifeq ($(PERF_DUMP), 1)
SOURCES += risc_perf.cc
endif

ifeq ("$(ARCH_NAME)","wormhole_b0")
SOURCES += $(BUDA_HOME)/src/firmware/riscv/wormhole/risc_chip_specific.cc
else
SOURCES += $(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/risc_chip_specific.cc
endif

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/wormhole/noc_nonblocking_api.cc
endif
ifeq ("$(ARCH_NAME)", "wormhole")
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc_nonblocking_api.cc
endif
ifeq ("$(ARCH_NAME)", "blackhole")
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc_nonblocking_api.cc
endif

FIRMWARE_NAME = ncrisc
INFO_NAME = ncrisc

INCLUDES = -I$(BUDA_HOME)/src/firmware/riscv/targets/ncrisc \
    -I$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME) \
	-I$(BUDA_HOME)/src/firmware/riscv/common \
  -I$(BUDA_HOME)/third_party/tt_llk_$(ARCH_NAME)/common/inc
	-I$(BUDA_HOME)

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/noc 
else
  INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc
endif

ifeq ("$(ARCH_NAME)", "blackhole")
  LINKER_SCRIPT_NAME = ncrisc-noiram.ld
else
  LINKER_SCRIPT_NAME = ncrisc.ld
endif
LINKER_SECTIONS_PREFIX = ncrisc

# uncomment to enable detailed logging in NCRISC post code for waveform debug
# DEFINES = -D ENABLE_NCRISC_DEBUG_POST_CODES

ifdef ($(NUM_EXEC_LOOP_ITERATIONS))
DEFINES += $(addprefix -DNUM_EXEC_LOOP_ITERATIONS=, $(NUM_EXEC_LOOP_ITERATIONS))
endif

ifeq ($(PERF_DUMP), 1)
DEFINES += $(addprefix -DPERF_DUMP=, $(PERF_DUMP))
DEFINES += $(addprefix -DPERF_DUMP_LEVEL=, $(PERF_DUMP_LEVEL))
endif

ifeq ($(DRAM_DECOUPLE), 1)
DEFINES += $(addprefix -DDRAM_DECOUPLE=, $(DRAM_DECOUPLE))
endif

#ifeq ($(DEVICE_RUNNER),silicon)
#DEFINES += -DRISC_GSYNC_ENABLED
#endif
ifeq ($(INTERMED_DUMP), 1)
DEFINES += $(addprefix -DINTERMED_DUMP=, $(INTERMED_DUMP))
endif

ifeq ($(PERF_DUMP_CONCURRENT), 1)
DEFINES += $(addprefix -DPERF_DUMP_CONCURRENT=, $(PERF_DUMP_CONCURRENT))
endif

ifeq ($(OVERLAY_DECOUPLE), 1)
DEFINES += $(addprefix -DOVERLAY_DECOUPLE=, $(OVERLAY_DECOUPLE))
endif

ifeq ("$(ARCH_NAME)", "wormhole_b0")
DEFINES += -DRISC_B0_HW
endif
ifeq ("$(ARCH_NAME)", "wormhole")
DEFINES += -DRISC_A0_HW
endif
ifeq ("$(ARCH_NAME)", "blackhole")
DEFINES += -DRISC_B0_HW
DEFINES += -DRISC_BLACKHOLE_HW
endif


# -fno-tree-loop-distribute-patterns is needed to prevent memset from being instantiated which gets forced in L0, which can get called from L1 functions
OPT_FLAGS = -flto -ffast-math -Os -g -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -frvtt-error-multdiv

include ../../toolchain/riscv.mk

all:: $(OUTPUT_DIR)/ncrisc.hex $(OUTPUT_DIR)/ncrisc.map $(OUTPUT_DIR)/ncrisc.fwlog $(OUTPUT_DIR)/ncrisc-decodedline.txt $(OUTPUT_DIR)/ncrisc-debuginfo.txt $(OUTPUT_DIR)/ncrisc-symbols.txt

clean::
	rm -rfd $(OUTPUT_DIR)/*
