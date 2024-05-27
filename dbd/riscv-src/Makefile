mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
mkfile_dir := $(dir $(mkfile_path))
TOOL_PATH?=$(mkfile_dir)../../third_party/sfpi/compiler/bin/

GXX=$(TOOL_PATH)/riscv32-unknown-elf-g++
OBJDUMP=$(TOOL_PATH)/riscv32-unknown-elf-objdump
OBJCOPY=$(TOOL_PATH)/riscv32-unknown-elf-objcopy
READELF=$(TOOL_PATH)/riscv32-unknown-elf-readelf

OPTIONS_ALL=-O0 -mwormhole -march=rv32imw -mtune=rvtt-b1 -mabi=ilp32 -std=c++17 -g -flto -ffast-math
OPTIONS_COMPILE=-fno-use-cxa-atexit -fno-exceptions -Wall -Werror -Wno-unknown-pragmas -Wno-error=multistatement-macros -Wno-error=parentheses -Wno-error=unused-but-set-variable -Wno-unused-variable -DARCH_WORMHOLE -DTENSIX_FIRMWARE -DLOCAL_MEM_EN=0 -DDEBUG_PRINT_ENABLED -DCOMPILE_FOR_BRISC
OPTIONS_LINK=-fno-exceptions -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -nostartfiles

all:
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_COMPILE) -c -o tmu-crt0.o tmu-crt0.S
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_COMPILE) -c -o brisc-no-globals.o brisc.cc
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_COMPILE) -DUSE_GLOBAL_VARS -c -o brisc-globals.o brisc.cc
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_LINK) -Tbrisc.ld tmu-crt0.o brisc-no-globals.o -o brisc-no-globals.elf
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_LINK) -Tbrisc.ld tmu-crt0.o brisc-globals.o -o brisc-globals.elf
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_LINK) -Tbrisc.ld tmu-crt0.o brisc-no-globals.o -o brisc-no-globals.elf
	$(GXX) $(OPTIONS_ALL) $(OPTIONS_LINK) -Tbrisc.ld tmu-crt0.o brisc-globals.o -o brisc-globals.elf
	$(READELF) --debug-dump ./brisc-no-globals.elf > brisc-no-globals.elf.dump
	$(READELF) --debug-dump ./brisc-globals.elf > brisc-globals.elf.dump
	$(OBJDUMP) -d -s -S brisc-no-globals.elf > brisc-no-globals.dis
	$(OBJDUMP) -t brisc-no-globals.elf | sort >> brisc-no-globals.dis
	$(OBJDUMP) -d -s -S brisc-globals.elf > brisc-globals.dis
	$(OBJDUMP) -t brisc-globals.elf | sort >> brisc-globals.dis
