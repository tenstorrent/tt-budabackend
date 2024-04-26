include $(BUDA_HOME)/infra/common.mk

ARCH_NAME ?= grayskull
DEVICE_TYPE=0

KERNELS ?=
UPCASE_KERNELS = $(shell echo $(KERNELS) | tr a-z A-Z)
KERNEL_DEFINES = $(addprefix -DUCK_, $(UPCASE_KERNELS))
DEFINES = $(KERNEL_DEFINES)

CKERNEL_DIR  = $(BUDA_HOME)/build/src/ckernels
FIRMWARE_RISCV_DIR = $(BUDA_HOME)/src/firmware/riscv
FIRMWARE_RISCV_INC_DIR = $(BUDA_HOME)/build/src/firmware/riscv
INCLUDES = \
	-I$(CKERNEL_DIR)/hlkc/inc \
	-I$(CKERNEL_DIR)/gen/out \
	-I$(FIRMWARE_RISCV_INC_DIR)/$(ARCH_NAME) \
	-I$(FIRMWARE_RISCV_INC_DIR)/$(ARCH_NAME)/noc \

ifeq ("$(ARCH_NAME)", "blackhole")
  INCLUDES += -I$(CKERNEL_DIR)/$(ARCH_NAME)/common/inc
else
  INCLUDES += -I$(BUDA_HOME)/third_party/tt_llk_$(ARCH_NAME)/common/inc
endif

DISABLE_FWLOG ?= 1

# provide in command line or kernel makefile
RISCV_TOOLCHAIN_DIR = $(BUDA_HOME)/src/firmware/riscv/toolchain
SOURCES ?= 
SOURCES += $(wildcard src/*.cc)
OUTPUT_DIR ?= out

.PHONY: build
build : $(OUTPUT_DIR)
	rm -Rf $(OUTPUT_DIR)  # always clean
	@mkdir -p $(OUTPUT_DIR)
	make hlkc_kernels
	@$(PRINT_SUCCESS)

include $(FIRMWARE_RISCV_DIR)/toolchain/riscv.mk
hlkc_kernels: $(OUTPUT_DIR) $(OUTPUT_DIR)/chlkc_unpack.o $(OUTPUT_DIR)/chlkc_math.o $(OUTPUT_DIR)/chlkc_pack.o

clean:
	rm -rf out
