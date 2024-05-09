# 1. Set SOURCES to your list of C/C++ sources (default main.cpp)
# 2. Override any other variables listed below.
# 3. Include this file.
# 4. Extend any targets as needed (all, extras, clean), see below.
#
# Output will be produced in $(OUTPUT_DIR)/$(FIRMWARE_NAME).hex (also .elf, .map).
#
# Variables you can override:
# - SOURCES: list of source files, paths relative to the Makefile, cpp, cc, c, S supported (main.cpp)
# - OPT_FLAGS: optimisation flags for C, C++ and C/C++ link (-flto -Os -g)
# - C_LANG_FLAGS: C language dialect & diagnostics (-Wall)
# - CXX_LANG_FLAGS: C++ language dialect & diagnostics (-Wall -std=c++14)
# - DEFINES: Preprocessor definitions for C, C++ and assembly ()
# - INCLUDES: additional include directories other than your source directories
# - OUTPUT_DIR: subdirectory for all outputs and temporary files, will be created if necessary (out)
# - FIRMWARE_NAME: firmware file name (firmware)
# - INFO_NAME: basename for ancillary files such as fwlog and debug info
# - FIRMWARE_START: start address of firmware (includes magic variables such as TEST_MAILBOX) (0)
#
# Targets you can extend: all, extras, clean. Use "::" instead of ":".

all:: # Always first to guarantee all is the default goal.

TOOLCHAIN := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

include $(BUDA_HOME)/infra/common.mk

SFPI ?= $(BUDA_HOME)/third_party/sfpi
RISCV_TOOLS_PREFIX := $(SFPI)/compiler/bin/riscv32-unknown-elf-
# RISCV_TOOLS_PREFIX := /proj_sw/ci/software/risc-v/riscv64-unknown-elf-gcc-8.3.0-2020.04.0-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-
CXX := $(CCACHE) $(RISCV_TOOLS_PREFIX)g++
CC  := $(CCACHE) $(RISCV_TOOLS_PREFIX)gcc
AS  := $(CCACHE) $(RISCV_TOOLS_PREFIX)gcc
OBJDUMP := $(RISCV_TOOLS_PREFIX)objdump
OBJCOPY := $(RISCV_TOOLS_PREFIX)objcopy

TDMA_ASSEMBLER_DIR := $(BUDA_HOME)/src/software/assembler
TDMA_ASSEMBLER := $(TDMA_ASSEMBLER_DIR)/out/assembler

FIRMWARE_NAME ?= firmware

LLK_TB_TEST ?= 0

DEP_FLAGS := -MD -MP
ifeq ($(FIRMWARE_NAME),tensix_thread2)
    OPT_FLAGS ?= -flto -ffast-math -O2
else 
    OPT_FLAGS ?= -flto -ffast-math -O3
endif

ifeq ($(KERNEL_DEBUG_SYMBOLS),1)
	OPT_FLAGS += -g
endif

# OPT_FLAGS ?= -flto -O2 -g
C_LANG_FLAGS ?= -Wall -Werror
# -fno-use-cax-atexit tells the compiler to use regular atexit for global object cleanup.
# Even for RISC-V, GCC compatible compilers use the Itanium ABI for some c++ things.
# atexit vs __cxa_atexit: https://itanium-cxx-abi.github.io/cxx-abi/abi.html#dso-dtor-runtime-api
#CXX_LANG_FLAGS ?= -Wall -Werror -std=c++17 -Wno-unknown-pragmas -fno-use-cxa-atexit -Wno-error=multistatement-macros
CXX_LANG_FLAGS ?= -Wall -Werror -std=c++17 -Wno-unknown-pragmas -fno-use-cxa-atexit -Wno-error=multistatement-macros -Wno-error=parentheses -Wno-error=unused-but-set-variable -Wno-unused-variable -fno-exceptions
DEFINES ?=
SOURCES ?= main.cpp

EPOCH_QUEUE_NUM_SLOTS ?= 0
EPOCH_ENTRY_SIZE_BYTES ?= 0
EPOCH_TABLE_DRAM_ADDRESS ?= 0
EPOCH_ALLOC_QUEUE_SYNC_ADDRESS ?= 0
KERNEL_CACHE_ENA ?= 0

ifeq ($(ENABLE_TT_LLK_DUMP), 1)
	DEFINES += -DENABLE_TT_LLK_DUMP
endif

ifeq ($(ENABLE_TT_LOG), 1)
    DEFINES += -DENABLE_TT_LOG
endif

ifeq ($(NO_DISTRIBUTED_EPOCH_TABLES),1)
	DEFINES += $(addprefix -DNO_DISTRIBUTED_EPOCH_TABLES=, $(NO_DISTRIBUTED_EPOCH_TABLES))
endif

ifeq ($(USE_EMULATOR_DRAM_LAYOUT),1)
	DEFINES += $(addprefix -DUSE_EMULATOR_DRAM_LAYOUT=, $(USE_EMULATOR_DRAM_LAYOUT))
endif

ifeq ($(LLK_TB_TEST), 1)
DEFINES += $(addprefix -DLLK_TB_TEST=, $(LLK_TB_TEST))
endif
DEFINES += -DFW_COMPILE

ifeq ($(ARCH_NAME),grayskull)
ARCH_FLAG := -mgrayskull -march=rv32iy -mtune=rvtt-b1
DEFINES += -DGRAYSKULL
else ifeq ($(ARCH_NAME),wormhole_b0)
ARCH_FLAG := -mwormhole -march=rv32imw -mtune=rvtt-b1
DEFINES += -DWORMHOLE_B0
else ifeq ($(ARCH_NAME),wormhole)
ARCH_FLAG := -mwormhole -mwormhole_a0 -march=rv32iw -mtune=rvtt-b1
DEFINES += -DWORMHOLE
else ifeq ($(ARCH_NAME),blackhole)
#FIXME MT: Does Blackhole need anything extra ??
ARCH_FLAG := -mwormhole -march=rv32imw -mtune=rvtt-b1 
DEFINES += -DBLACKHOLE
endif

ifneq ($(EPOCH_QUEUE_NUM_SLOTS), 0)
DEFINES += $(addprefix -DEPOCH_QUEUE_NUM_SLOTS=, $(EPOCH_QUEUE_NUM_SLOTS))
DEFINES += $(addprefix -DEPOCH_ENTRY_SIZE_BYTES=, $(EPOCH_ENTRY_SIZE_BYTES))
DEFINES += $(addprefix -DEPOCH_TABLE_DRAM_ADDRESS=, $(EPOCH_TABLE_DRAM_ADDRESS))
DEFINES += $(addprefix -DEPOCH_ALLOC_QUEUE_SYNC_ADDRESS=, $(EPOCH_ALLOC_QUEUE_SYNC_ADDRESS))
endif

ifneq ($(KERNEL_CACHE_ENA),0)
	DEFINES += $(addprefix -DKERNEL_CACHE_ENA=, $(KERNEL_CACHE_ENA))
endif

TRISC_L0_EN ?= 0 
ARCH := -mabi=ilp32 $(ARCH_FLAG)
# ARCH := -march=rv32i -mabi=ilp32
LINKER_SECTIONS_PREFIX ?= tensix
LINKER_SCRIPT_NAME ?= tensix.ld
LINKER_SCRIPT := $(TOOLCHAIN)/$(LINKER_SCRIPT_NAME)
LINKER_SECTIONS_PRE_PROC := $(TOOLCHAIN)/$(LINKER_SECTIONS_PREFIX)-sections-pre-proc.ld
LINKER_SECTIONS := $(TOOLCHAIN)/$(LINKER_SECTIONS_PREFIX)-sections.ld
DEFS := -DTENSIX_FIRMWARE -DLOCAL_MEM_EN=$(TRISC_L0_EN)

ifdef NUM_EXEC_LOOP_ITERATIONS
	DEFINES += $(addprefix -DNUM_EXEC_LOOP_ITERATIONS=, $(NUM_EXEC_LOOP_ITERATIONS))
endif
ARCH_NAME ?= grayskull

OUTPUT_DIR ?= $(BUDA_HOME)/build/src/firmware/riscv/targets/$(FIRMWARE_NAME)/out
GEN_DIR := gen
INFO_NAME ?= $(FIRMWARE_NAME)
FIRMWARE_START ?= 0
TRISC0_SIZE ?= 16384
TRISC1_SIZE ?= 8192
TRISC2_SIZE ?= 20480
TRISC_BASE  ?= 27648

# Configurable base addresses into epoch binary in DRAM for NCRISC FW which depend on whether Kernel Cache is enabled.
ifdef DRAM_EPOCH_RUNTIME_CONFIG_BASE
	DEFINES += $(addprefix -DDRAM_EPOCH_RUNTIME_CONFIG_BASE=, $(DRAM_EPOCH_RUNTIME_CONFIG_BASE))
endif
ifdef DRAM_OVERLAY_BLOB_BASE
	DEFINES += $(addprefix -DDRAM_OVERLAY_BLOB_BASE=, $(DRAM_OVERLAY_BLOB_BASE))
endif

# All objects are dumped into out, so we don't support two source files in different directories with the same name.
ifneq ($(words $(sort $(SOURCES))),$(words $(sort $(notdir $(SOURCES)))))
$(error $$(SOURCES) contains a duplicate filename)
endif

# Derive the list of source directories from $(SOURCES), use that as a list of include directories.
SOURCE_DIRS := $(filter-out ./,$(sort $(dir $(SOURCES))))
INCLUDES := $(INCLUDES) -I "$(BUDA_HOME)" -I "$(SFPI)/include" -I "$(BUDA_HOME)/src/firmware/riscv/common" $(addprefix -iquote ,$(SOURCE_DIRS)) -iquote .

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  INCLUDES += -I "$(BUDA_HOME)/src/firmware/riscv/wormhole"
  INCLUDES += -I "$(BUDA_HOME)/src/firmware/riscv/wormhole/noc"
  INCLUDES += -I "$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_b0_defines"
else
  INCLUDES += -I "$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)"
  INCLUDES += -I "$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc"
endif

ifeq ("$(ARCH_NAME)", "wormhole")
  INCLUDES += $(INCLUDES) -I "$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_a0_defines"
endif

# These are deferred so I can adjust DEP_FLAGS in the dependency-generation-only rules
CXXFLAGS = $(ARCH) $(DEP_FLAGS) $(OPT_FLAGS) $(CXX_LANG_FLAGS) $(DEFINES) $(DEFS) $(INCLUDES)
CFLAGS = $(ARCH) $(DEP_FLAGS) $(OPT_FLAGS) $(C_LANG_FLAGS) $(DEFINES) $(DEFS) $(INCLUDES)

LDFLAGS := $(ARCH) $(OPT_FLAGS) -Wl,--gc-sections -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -Wl,--defsym=__firmware_start=$(FIRMWARE_START) \
-Wl,--defsym=__trisc_base=$(TRISC_BASE) -Wl,--defsym=__trisc0_size=$(TRISC0_SIZE) -Wl,--defsym=__trisc1_size=$(TRISC1_SIZE) -Wl,--defsym=__trisc2_size=$(TRISC2_SIZE) \
-T$(LINKER_SCRIPT) -L$(TOOLCHAIN) -nostartfiles

OUTFW := $(OUTPUT_DIR)/$(FIRMWARE_NAME)

OBJECTS := $(addprefix $(OUTPUT_DIR)/, $(addsuffix .o,$(basename $(notdir $(SOURCES)))))
DEPENDS := $(addprefix $(OUTPUT_DIR)/, $(addsuffix .d,$(basename $(notdir $(SOURCES)))))

ifneq ("$(BUILD_TARGET)", "ETH_APP")
  EXTRA_OBJECTS += $(OUTPUT_DIR)/substitutes.o $(OUTPUT_DIR)/tmu-crt0.o
endif

vpath % $(subst $(space),:,$(TOOLCHAIN) $(SOURCE_DIRS))


#stopping bin generation for now
all:: extras $(OUTFW).hex $(OUTFW).map #$(OUTFW).bin  
	@$(PRINT_SUCCESS)

$(GEN_DIR):
	-mkdir -p $@

ifeq ($(ENABLE_TT_LOG), 1)
# These dependency-generation-only rules are here so we can rebuild dependencies
# without running the full compiler, and thereby discover generated header
# files (but not their dependencies).

# Special DEP_FLAGS to scan deps only, not compile. This variable will
# be applied whenever building *.d.
$(OUTPUT_DIR)/%.d: DEP_FLAGS = -M -MP -MG -MT $(addsuffix .o,$(basename $@))

# For C or C++ sources, regenerate dependencies by running the compiler with
# the special DEP_FLAGS above.
$(OUTPUT_DIR)/%.d: %.c | $(OUTPUT_DIR)
	@$(CXX) $(CXXFLAGS) -x c++ -c -o $@ $<

$(OUTPUT@_DIR)/%.d: %.cpp | $(OUTPUT_DIR)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUTPUT_DIR)/%.d: %.cc | $(OUTPUT_DIR)
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# All objects depend on the dependency listing. Maybe this should be an order-only rule.
$(OUTPUT_DIR)/%.o: $(OUTPUT_DIR)/%.d
endif

# Assemble tensix assembly into an array fragment
$(GEN_DIR)/%.asm.h: %.asm | $(GEN_DIR)
	@$(TDMA_ASSEMBLER) --out-array $@ $<

$(OUTPUT_DIR):
	@-mkdir -p $@

# Assemble RISC-V sources using C compiler
$(OUTPUT_DIR)/%.o: %.S | $(OUTPUT_DIR)
	echo "AS $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Compile C
$(OUTPUT_DIR)/%.o: %.c | $(OUTPUT_DIR)
	echo "CC $<"
	@$(CC) $(CXXFLAGS) -x c++ -c -o $@ $<

# Compile C++
$(OUTPUT_DIR)/%.o: %.cpp | $(OUTPUT_DIR)
	echo "CXX $<"
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile C++
$(OUTPUT_DIR)/%.o: %.cc | $(OUTPUT_DIR)
	echo "$(CXX) $(CXXFLAGS) -c -o $@ $<"
	echo "CXX $<"
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# Link using C++ compiler
$(OUTFW).elf: $(OBJECTS) $(EXTRA_OBJECTS) $(LINKER_SCRIPT)
	echo "$(CKERNELS)"
	# @echo "$(CXX) -E -x c -D __trisc_local_mem_en=$(TRISC_L0_EN) -P $(LINKER_SECTIONS_PRE_PROC) -o $(LINKER_SECTIONS)"
	echo "$(CXX) $(LDFLAGS) -o $@ $(filter-out $(LINKER_SCRIPT),$^)"
	#$(CXX) -E -x c -D __trisc_local_mem_en=$(TRISC_L0_EN) -P $(LINKER_SECTIONS_PRE_PROC) -o $(LINKER_SECTIONS)
	@$(CXX) $(LDFLAGS) -o $@ $(filter-out $(LINKER_SCRIPT),$^)
	@chmod -x $@

# Generate hex-formatted firmware for LUA
$(OUTFW).hex: $(OUTFW).elf
	@$(OBJCOPY) -O verilog $< $@.tmp
	@python3 $(TOOLCHAIN)/hex8tohex32.py $@.tmp > $@
	@rm $@.tmp

$(OUTFW).bin: $(OUTFW).elf
	@$(OBJCOPY) -R .data -O binary $< $@

# Symbol map
$(OUTFW).map: $(OUTFW).elf
	@$(OBJDUMP) -dSxCl $< > $@

ifeq ($(ENABLE_TT_LOG), 1)

CKERNEL_DEPS := $(wildcard $(OUTPUT_DIR)/*.d)

# Create a file that maps where we log in firmware source to what is being logged
# IMPROVE: handle multiple source files
$(OUTPUT_DIR)/$(INFO_NAME).fwlog: $(OUTFW).elf
	@python3 $(BUDA_HOME)/src/firmware/riscv/toolchain/fwlog.py --depfile $(OUTPUT_DIR)/$(INFO_NAME).d > $@

$(OUTPUT_DIR)/ckernel.fwlog: $(OUTFW).elf $(CKERNEL_DEPS) $(OUTPUT_DIR)/ckernel.d
	@python3 $(BUDA_HOME)/src/firmware/riscv/toolchain/fwlog.py --depfile $(OUTPUT_DIR)/ckernel.d > $@.tmp
	@python3 $(BUDA_HOME)/src/firmware/riscv/toolchain/fwlog.py --depfile $(CKERNEL_DEPS) --path=$(CKERNELS_DIR)/$(ARCH_NAME)/src >> $@.tmp
	@python3 $(BUDA_HOME)/src/firmware/riscv/toolchain/fwlog.py --depfile $(CKERNELS_DIR)/$(ARCH_NAME)/common/src/fwlog_list --path=$(CKERNELS_DIR)/$(ARCH_NAME)/common/src >> $@.tmp
	@sort -u $@.tmp > $@   # uniquify
	@rm -f $@.tmp
else
.PHONY: $(OUTPUT_DIR)/$(INFO_NAME).fwlog $(OUTPUT_DIR)/ckernel.fwlog
$(OUTPUT_DIR)/$(INFO_NAME).fwlog:
$(OUTPUT_DIR)/ckernel.fwlog:
endif

# Create a map between source files and the PC
$(OUTPUT_DIR)/$(INFO_NAME)-decodedline.txt: $(OUTFW).elf
	@$(RISCV_TOOLS_PREFIX)readelf --debug-dump=decodedline $< > $@

# Create a map between source files and the PC
$(OUTPUT_DIR)/$(INFO_NAME)-debuginfo.txt: $(OUTFW).elf
	@$(RISCV_TOOLS_PREFIX)readelf --debug-dump=info $< > $@
#	python ./parse_labels.py --infile out/firmware-debuginfo.txt

# Create a symbol table dump
$(OUTPUT_DIR)/$(INFO_NAME)-symbols.txt: $(OUTFW).elf
	@$(RISCV_TOOLS_PREFIX)objdump -t $< > $@

clean2::
	rm $(OUTFW).elf $(OUTFW).hex $(OUTFW).bin $(OUTFW).map $(SILENT_ERRORS)
	rm $(OBJECTS) $(SILENT_ERRORS)
	rm $(DEPENDS) $(SILENT_ERRORS)
	rm $(EXTRA_OBJECTS) $(SILENT_ERRORS)
	rm $(EXTRA_OBJECTS:.o=.d) $(SILENT_ERRORS)
	rmdir $(OUTPUT_DIR) $(SILENT_ERRORS)
	rm $(OUTPUT_DIR)/$(INFO_NAME).fwlog $(OUTPUT_DIR)/$(INFO_NAME)-decodedline.txt $(OUTPUT_DIR)/$(INFO_NAME)-debuginfo.txt $(OUTPUT_DIR)/$(INFO_NAME)-symbols.txt $(SILENT_ERRORS) 
	rm -rf $(GEN_DIR) $(SILENT_ERRORS)

extras::

.PHONY: clean2 all extras

-include $(DEPENDS)
