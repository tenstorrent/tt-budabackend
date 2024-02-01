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

ifeq ($(PERF_DUMP), 1)
DEFINES += $(addprefix -DPERF_DUMP=, $(PERF_DUMP))
DEFINES += $(addprefix -DPERF_DUMP_LEVEL=, $(PERF_DUMP_LEVEL))
endif
ifeq ($(PERF_DUMP_CONCURRENT), 1)
DEFINES += $(addprefix -DPERF_DUMP_CONCURRENT=, $(PERF_DUMP_CONCURRENT))
endif
ifeq ($(INTERMED_DUMP), 1)
DEFINES += $(addprefix -DINTERMED_DUMP=, $(INTERMED_DUMP))
endif
ifeq ($(OVERLAY_DECOUPLE), 1)
DEFINES += $(addprefix -DOVERLAY_DECOUPLE=, $(OVERLAY_DECOUPLE))
endif

SOURCES =\
	$(BUDA_HOME)/src/firmware/riscv/common/risc_common.cc \
	$(BUDA_HOME)/src/firmware/riscv/common/unpack_pack_stream_intf.cc \
	brisc.cc

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/wormhole/tdma_xmov.c
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/wormhole/noc/noc.c
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/wormhole/risc_chip_specific.cc
else
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/tdma_xmov.c
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc/noc.c
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

FIRMWARE_NAME = brisc
INFO_NAME = brisc

INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/targets/ncrisc \
	-I$(BUDA_HOME)/src/ckernels/$(ARCH_NAME)/common/inc \
	-I$(BUDA_HOME)

LINKER_SCRIPT_NAME = brisc.ld
LINKER_SECTIONS_PREFIX = brisc

# -fno-tree-loop-distribute-patterns is needed to prevent memset from being instantiated which gets forced in L0, which can get called from L1 functions
OPT_FLAGS = -flto -ffast-math -Os -g -fno-tree-loop-distribute-patterns -frvtt-error-multdiv

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

include ../../toolchain/riscv.mk

clean:
	@rm -rfd $(OUTPUT_DIR)

all:: $(OUTPUT_DIR)/brisc.hex $(OUTPUT_DIR)/brisc.map $(OUTPUT_DIR)/brisc.fwlog $(OUTPUT_DIR)/brisc-decodedline.txt $(OUTPUT_DIR)/brisc-debuginfo.txt $(OUTPUT_DIR)/brisc-symbols.txt

