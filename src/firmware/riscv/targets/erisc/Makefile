include $(BUDA_HOME)/infra/common.mk

ARCH_NAME ?= grayskull
OVERLAY_DECOUPLE ?= 0

SOURCES = src/erisc_main.cc \
          src/erisc_app.cc \
          src/api/eth_init.cpp \
          src/api/eth_ss.cpp \
          src/rtos/rtos.cc \
          src/rtos/rtosASM.S \
          src/eth_routing_v2.cpp

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

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/wormhole/noc/noc.c
else
  SOURCES += $(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc/noc.c
endif

FIRMWARE_NAME = erisc

DEFINES += -DETH_TEST_FW -DETH_INIT_FW -DERISC
ifeq ("$(ARCH_NAME)", "wormhole")
DEFINES += -DRISC_A0_HW
endif
ifeq ($(OVERLAY_DECOUPLE), 1)
DEFINES += $(addprefix -DOVERLAY_DECOUPLE=, $(OVERLAY_DECOUPLE))
endif

LINKER_SCRIPT_NAME := erisc.ld

XXD=xxd
SED=sed

INCLUDES = -Irelease

OPT_FLAGS = -flto -ffast-math -Os -g

CXX_LANG_FLAGS = -Wall -std=c++17 -Wno-unknown-pragmas -fno-use-cxa-atexit -Wno-error=multistatement-macros -fno-delete-null-pointer-checks

ifdef NUM_EXEC_LOOP_ITERATIONS
	DEFINES += $(addprefix -DNUM_EXEC_LOOP_ITERATIONS=, $(NUM_EXEC_LOOP_ITERATIONS))
endif

include ../../toolchain/riscv.mk

all :: $(OUTPUT_DIR)/$(FIRMWARE_NAME).srec $(OUTPUT_DIR)/$(FIRMWARE_NAME).map $(OUTPUT_DIR)/$(FIRMWARE_NAME).bin
	@$(MAKE) -f Makefile.eth_app

$(OUTPUT_DIR)/$(FIRMWARE_NAME).srec: $(OUTPUT_DIR)/$(FIRMWARE_NAME).elf
	@$(OBJCOPY) -O srec $< $@

app ::
	@$(MAKE) -f Makefile.eth_app --always-make

clean::
	$(MAKE) -f Makefile.eth_app clean
	rm -fdr $(OUTPUT_DIR)


