
KERNEL_PARAM_OVERRIDE_DIR ?= 
TEST_NAME ?= 
KERNELS ?= cblank
UPCASE_KERNELS = $(shell echo $(KERNELS) | tr a-z A-Z)
KERNEL_DEFINES = $(addprefix -DUCK_, $(UPCASE_KERNELS))
DEFINES = $(KERNEL_DEFINES)
PERF_DUMP ?= 0
LLK_TB_TEST ?= 0


ifeq ($(PERF_DUMP), 1)
DEFINES += $(addprefix -DPERF_DUMP=, $(PERF_DUMP))
endif

ifeq ($(LLK_TB_TEST), 1)
DEFINES += $(addprefix -DLLK_TB_TEST=, $(LLK_TB_TEST))
endif

ARCH_NAME ?= grayskull
DEVICE_TYPE=0

CKERNEL_DIR  = $(BUDA_HOME)/src/ckernels/
CKERNEL_INC_DIR  = $(BUDA_HOME)/build/src/ckernels/
CKERNEL_INC_OVERRIDE_DIR  ?= $(BUDA_HOME)/build/src/ckernels/
OUTPUT_DIR = out
FIRMWARE_RISCV_DIR = $(BUDA_HOME)/src/firmware/riscv
FIRMWARE_RISCV_INC_DIR = $(BUDA_HOME)/build/src/firmware/riscv
INCLUDES = \
	-I$(CKERNEL_INC_OVERRIDE_DIR)/$(ARCH_NAME)/buda/common/inc \
	-I$(KERNEL_PARAM_OVERRIDE_DIR)/ \
	-I$(BUDA_HOME)/build/tests/$(TEST_NAME) \
	-I$(CKERNEL_INC_OVERRIDE_DIR)/$(ARCH_NAME)/src/test_params/ \
	-I$(CKERNEL_INC_DIR)/gen/out \
	-I$(CKERNEL_INC_OVERRIDE_DIR)/$(ARCH_NAME)/buda/llk_io/ \
	-I$(CKERNEL_INC_OVERRIDE_DIR)/$(ARCH_NAME)/buda/llk_api/ \
	-I$(FIRMWARE_RISCV_INC_DIR)/$(ARCH_NAME) \
	-I$(FIRMWARE_RISCV_INC_DIR)/$(ARCH_NAME)/noc \

ifeq ("$(ARCH_NAME)", "grayskull")
  	INCLUDES += -I$(BUDA_HOME)/third_party/tt_llk_grayskull/common/inc
  	INCLUDES += -I$(BUDA_HOME)/third_party/tt_llk_grayskull/llk_lib/
else
	INCLUDES += -I$(CKERNEL_INC_OVERRIDE_DIR)/$(ARCH_NAME)/buda/common/inc
	INCLUDES += -I$(CKERNEL_INC_OVERRIDE_DIR)/$(ARCH_NAME)/llk_lib/
endif

VPATH=out

.PHONY: build
build:
	rm -Rf $(OUTPUT_DIR)
	mkdir -p $(OUTPUT_DIR)
	$(MAKE) -C $(CKERNEL_DIR)/$(ARCH_NAME)/buda/common KERNELS=$(KERNELS)
	$(MAKE) $(OUTPUT_DIR)/$(KERNELS).o

clean::
	rm -Rf $(OUTPUT_DIR)

# .d files only get created if TT_LOG or TT_LLK_DUMP is enabled
DEPS_EXIST = 0
ifeq ($(ENABLE_TT_LOG), 1)
	DEPS_EXIST = 1
endif
ifeq ($(ENABLE_TT_LLK_DUMP), 1)
	DEPS_EXIST = 1
endif

ifeq ($(DEPS_EXIST), 1)
-include $(OUTPUT_DIR)/$(KERNELS).d
endif
include $(CKERNEL_INC_DIR)/gen/out/ckernel.mk
include $(FIRMWARE_RISCV_DIR)/toolchain/riscv.mk

