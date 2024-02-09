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

TOOLCHAIN := $(BUDA_HOME)/src/pkernels/toolchain

include $(BUDA_HOME)/infra/common.mk

SFPI ?= $(BUDA_HOME)/third_party/sfpi
RISCV_TOOLS_PREFIX := $(SFPI)/compiler/bin/riscv32-unknown-elf-
# RISCV_TOOLS_PREFIX := /home/software/risc-v/riscv64-unknown-elf-gcc-8.3.0-2020.04.0-x86_64-linux-ubuntu14/bin/riscv64-unknown-elf-
CXX := $(CCACHE) $(RISCV_TOOLS_PREFIX)g++
CC  := $(CCACHE) $(RISCV_TOOLS_PREFIX)gcc
AS  := $(CCACHE) $(RISCV_TOOLS_PREFIX)gcc
OBJDUMP := $(RISCV_TOOLS_PREFIX)objdump
OBJCOPY := $(RISCV_TOOLS_PREFIX)objcopy

FIRMWARE_NAME ?= firmware
OUTPUT_DIR ?= pkernels/$(FIRMWARE_NAME)

DEP_FLAGS := -MD -MP
OPT_FLAGS ?= -flto -ffast-math -O2 -fPIC

ifeq ($(KERNEL_DEBUG_SYMBOLS),1)
	OPT_FLAGS += -g
endif

C_LANG_FLAGS ?= -Wall -Werror
CXX_LANG_FLAGS ?= -Wall -Werror -std=c++17 -Wno-unknown-pragmas -fno-use-cxa-atexit -Wno-error=multistatement-macros -Wno-error=parentheses -Wno-error=unused-but-set-variable -Wno-unused-variable -fno-exceptions
DEFINES ?=
SOURCES ?= $(OUTPUT_DIR)/$(FIRMWARE_NAME).cc

EPOCH_QUEUE_NUM_SLOTS ?= 0
EPOCH_ENTRY_SIZE_BYTES ?= 0
EPOCH_TABLE_DRAM_ADDRESS ?= 0
EPOCH_ALLOC_QUEUE_SYNC_ADDRESS ?= 0

ifeq ($(NO_DISTRIBUTED_EPOCH_TABLES),1)
	DEFINES += $(addprefix -DNO_DISTRIBUTED_EPOCH_TABLES=, $(NO_DISTRIBUTED_EPOCH_TABLES))
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
endif

ifneq ($(EPOCH_QUEUE_NUM_SLOTS), 0)
DEFINES += $(addprefix -DEPOCH_QUEUE_NUM_SLOTS=, $(EPOCH_QUEUE_NUM_SLOTS))
DEFINES += $(addprefix -DEPOCH_ENTRY_SIZE_BYTES=, $(EPOCH_ENTRY_SIZE_BYTES))
DEFINES += $(addprefix -DEPOCH_TABLE_DRAM_ADDRESS=, $(EPOCH_TABLE_DRAM_ADDRESS))
DEFINES += $(addprefix -DEPOCH_ALLOC_QUEUE_SYNC_ADDRESS=, $(EPOCH_ALLOC_QUEUE_SYNC_ADDRESS))
endif

ARCH := -mabi=ilp32 $(ARCH_FLAG)
# ARCH := -march=rv32i -mabi=ilp32
LINKER_SECTIONS_PREFIX ?= pkernels
LINKER_SCRIPT_NAME ?= pkernels.ld
LINKER_SCRIPT := $(TOOLCHAIN)/$(LINKER_SCRIPT_NAME)
LINKER_SECTIONS := $(TOOLCHAIN)/$(LINKER_SECTIONS_PREFIX)-sections.ld
DEFS := -DTENSIX_FIRMWARE 

ARCH_NAME ?= grayskull

INFO_NAME ?= $(FIRMWARE_NAME)
FIRMWARE_START ?= 0

# All objects are dumped into out, so we don't support two source files in different directories with the same name.
ifneq ($(words $(sort $(SOURCES))),$(words $(sort $(notdir $(SOURCES)))))
$(error $$(SOURCES) contains a duplicate filename)
endif

# Derive the list of source directories from $(SOURCES), use that as a list of include directories.
SOURCE_DIRS := $(filter-out ./,$(sort $(dir $(SOURCES))))
INCLUDES := $(INCLUDES) -I "$(BUDA_HOME)" -I "$(SFPI)/include" -I "$(BUDA_HOME)/src/firmware/riscv/common" -I "$(BUDA_HOME)/src/pkernels/common" $(addprefix -iquote ,$(SOURCE_DIRS)) -iquote .

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

LDFLAGS := $(ARCH) $(OPT_FLAGS) -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -Wl,--defsym=__firmware_start=$(FIRMWARE_START) \
-T$(LINKER_SCRIPT) -L$(TOOLCHAIN) -nostartfiles

OUTFW := $(OUTPUT_DIR)/$(FIRMWARE_NAME)

OBJECTS := $(addprefix $(OUTPUT_DIR)/, $(addsuffix .o,$(basename $(notdir $(SOURCES)))))
DEPENDS := $(addprefix $(OUTPUT_DIR)/, $(addsuffix .d,$(basename $(notdir $(SOURCES)))))

vpath % $(subst $(space),:,$(TOOLCHAIN) $(SOURCE_DIRS))


ifeq ($(VERBOSE_BUILD), 1)
all:: extras $(OUTFW).hex $(OUTFW).map 
	@$(PRINT_SUCCESS)
else
all:: extras $(OUTFW).hex $(OUTFW).map 
	rm $(OUTPUT_DIR)/*.o $(OUTPUT_DIR)/*.elf $(OUTPUT_DIR)/*.d
endif

$(OUTPUT_DIR):
	@-mkdir -p $@

# Assemble RISC-V sources using C compiler
$(OUTPUT_DIR)/%.o: %.S | $(OUTPUT_DIR)
	# echo "AS $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Compile C
$(OUTPUT_DIR)/%.o: %.c | $(OUTPUT_DIR)
	# echo "CC $<"
	@$(CC) $(CXXFLAGS) -x c++ -c -o $@ $<

# Compile C++
$(OUTPUT_DIR)/%.o: %.cpp | $(OUTPUT_DIR)
	# echo "CXX $<"
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile C++
$(OUTPUT_DIR)/%.o: %.cc | $(OUTPUT_DIR)
	# echo "$(CXX) $(CXXFLAGS) -c -o $@ $<"
	# echo "CXX $<"
	@$(CXX) $(CXXFLAGS) -c -o $@ $<

# Link using C++ compiler
$(OUTFW).elf: $(OBJECTS) $(EXTRA_OBJECTS) $(LINKER_SCRIPT)
	# echo "$(CKERNELS)"
	# echo "$(CXX) $(LDFLAGS) -o $@ $(filter-out $(LINKER_SCRIPT),$^)"
	@$(CXX) $(LDFLAGS) -o $@ $(filter-out $(LINKER_SCRIPT),$^)
	@chmod -x $@

# Generate hex-formatted firmware for LUA
# The sed command changes mret to ret
$(OUTFW).hex: $(OUTFW).elf
	@$(OBJCOPY) -O verilog $< $@.tmp
	@python3 $(TOOLCHAIN)/hex8tohex32.py $@.tmp > $@
	@rm $@.tmp
	@sed -i 's/30200073/00008067/g' $@
	@sed -i '1d' $@

# Symbol map
$(OUTFW).map: $(OUTFW).elf
	@$(OBJDUMP) -dSxCl $< > $@

# Create a map between source files and the PC
$(OUTPUT_DIR)/$(INFO_NAME)-decodedline.txt: $(OUTFW).elf
	@$(RISCV_TOOLS_PREFIX)readelf --debug-dump=decodedline $< > $@

# Create a map between source files and the PC
$(OUTPUT_DIR)/$(INFO_NAME)-debuginfo.txt: $(OUTFW).elf
	@$(RISCV_TOOLS_PREFIX)readelf --debug-dump=info $< > $@

# Create a symbol table dump
$(OUTPUT_DIR)/$(INFO_NAME)-symbols.txt: $(OUTFW).elf
	@$(RISCV_TOOLS_PREFIX)objdump -t $< > $@

clean2::
	rm -rf $(OUTPUT_DIR) $(SILENT_ERRORS)

extras::

.PHONY: clean2 all extras

-include $(DEPENDS)
