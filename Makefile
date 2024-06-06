.SUFFIXES:


# nproc can result in OOM errors for specific machines and should be reworked.
#MAKEFLAGS := --jobs=$(shell nproc)

# Setup CONFIG, DEVICE_RUNNER, and out/build dirs first
BUDA_HOME ?= $(shell git rev-parse --show-toplevel)
CONFIG ?= develop
ARCH_NAME ?= grayskull
HOST_ARCH = $(shell uname -m)
# TODO: enable OUT to be per config (this impacts all scripts that run tests)
# OUT ?= build_$(DEVICE_RUNNER)_$(CONFIG)
OUT ?= $(BUDA_HOME)/build
PREFIX ?= $(OUT)
TT_MODULES=
BACKEND_PROFILER_DEBUG ?= 0

GIT_BRANCH = $(shell git rev-parse --abbrev-ref HEAD)
GIT_HASH = $(shell git describe --always --dirty)

CONFIG_CFLAGS =
CONFIG_LDFLAGS =
OPT_LEVEL=
DEBUG_FLAGS=
UMD_VERSIM_STUB ?= 1

ifeq ($(CONFIG), release)
OPT_LEVEL = -O3 -fno-lto
else ifeq ($(CONFIG), develop)
OPT_LEVEL = -O3 -fno-lto
DEBUG_FLAGS += -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), ci)  # significantly smaller artifacts
OPT_LEVEL = -O3
DEBUG_FLAGS += -DTT_DEBUG -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), assert)
OPT_LEVEL = -O3 -g
DEBUG_FLAGS += -DTT_DEBUG -DTT_DEBUG_LOGGING
else ifeq ($(CONFIG), asan)
OPT_LEVEL = -O3 -g
DEBUG_FLAGS += -DTT_DEBUG -DTT_DEBUG_LOGGING -fsanitize=address
CONFIG_LDFLAGS += -fsanitize=address
else ifeq ($(CONFIG), ubsan)
OPT_LEVEL = -O3 -g
DEBUG_FLAGS += -DTT_DEBUG -DTT_DEBUG_LOGGING -fsanitize=undefined
CONFIG_LDFLAGS += -fsanitize=undefined
else ifeq ($(CONFIG), debug)
OPT_LEVEL = -O0 -g
DEBUG_FLAGS += -DDEBUG -DTT_DEBUG -DTT_DEBUG_LOGGING
else
$(error Unknown value for CONFIG "$(CONFIG)")
endif

ENABLE_CODE_TIMERS ?= 1

ifeq ($(ENABLE_CODE_TIMERS), 1)
DEBUG_FLAGS += -DTT_ENABLE_CODE_TIMERS
endif

ifeq ($(BACKEND_PROFILER_DEBUG), 1)
DEBUG_FLAGS += -DBACKEND_PROFILER_DEBUG
endif

CONFIG_CFLAGS += $(OPT_LEVEL)
CONFIG_CFLAGS += $(DEBUG_FLAGS)

OBJDIR = $(OUT)/obj
LIBDIR = $(OUT)/lib
BINDIR = $(OUT)/bin
INCDIR = $(OUT)/include
TESTDIR = $(OUT)/test
UTSDIR = $(OUT)/unit_tests
DOCSDIR = $(OUT)/docs
TARGETSDIR = $(OUT)/perf_targets


# Top level flags, compiler, defines etc.

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  BASE_INCLUDES=-Isrc/firmware/riscv/wormhole -Isrc/firmware/riscv/wormhole/wormhole_b0_defines
else ifeq ("$(ARCH_NAME)", "wormhole")
  BASE_INCLUDES=-Isrc/firmware/riscv/wormhole -Isrc/firmware/riscv/wormhole/wormhole_a0_defines
else
  BASE_INCLUDES=-Isrc/firmware/riscv/$(ARCH_NAME)
endif

CCACHE := $(shell command -v ccache 2> /dev/null)

# Ccache setup
ifneq ($(CCACHE),)
  ifdef CCACHE_DIR
    $(info "CCACHE_DIR is set to $(CCACHE_DIR)")
  else
    $(info "CCACHE_DIR is not set, using default $(HOME)/.ccache")
    export CCACHE_DIR=$(HOME)/.ccache
  endif
  export CCACHE_MAXSIZE=10
  export CCACHE_UMASK=002
  export CCACHE_COMPRESS=true
  export CCACHE_NOHARDLINK=true
  export CCACHE_DIR_EXISTS=$(shell [ -d $(CCACHE_DIR) ] && echo "1")
  ifneq ($(CCACHE_DIR_EXISTS), 1)
    $(info $(HOME)/.ccache directory does not exist, creating it.")
    $(shell mkdir -p $(CCACHE_DIR))
  endif
  #CCACHE_CMD=$(CCACHE)
  #$(info CCACHE IS ENABLED)
#else
  #$(warning "ccache is not installed, please reach out to devops if it's expected.")
  #$(info CCACHE IS DISABLED)
endif

ifeq ("$(HOST_ARCH)", "aarch64")
# Include yaml from system libraries if compiling for ARM
BASE_INCLUDES+=-I/usr/include/yaml-cpp
YAML_PATH=/usr/include/yaml-cpp
else
# Get yaml-cpp headers from Versim submodule for x86
BASE_INCLUDES+=-I$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/yaml-cpp/include
YAML_PATH=$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/yaml-cpp/include
endif

#WARNINGS ?= -Wall -Wextra
WARNINGS ?= -Werror -Wdelete-non-virtual-dtor -Wreturn-type -Wswitch -Wuninitialized -Wno-unused-parameter
CC ?= $(CCACHE_CMD) gcc
CXX ?= $(CCACHE_CMD) g++
DEVICE_CXX = g++
ifeq ("$(HOST_ARCH)", "aarch64")
CFLAGS ?= -MMD $(WARNINGS) -I. $(CONFIG_CFLAGS) -DBUILD_DIR=\"$(OUT)\" -DFMT_HEADER_ONLY -Ithird_party/fmt
else
# Add AVX and FMA (Fused Multiply Add) flags for x86 compile
CFLAGS ?= -MMD $(WARNINGS) -I. $(CONFIG_CFLAGS) -mavx2 -mfma -DBUILD_DIR=\"$(OUT)\" -DFMT_HEADER_ONLY -Ithird_party/fmt
endif
CXXFLAGS ?= --std=c++17 -fvisibility-inlines-hidden
LDFLAGS ?= $(CONFIG_LDFLAGS) -Wl,-rpath,$(PREFIX)/lib -L$(LIBDIR) -ldl
SHARED_LIB_FLAGS = -shared -fPIC
STATIC_LIB_FLAGS = -fPIC

COREMODEL_SPARTA_LIBS = -lyaml-cpp -lpthread -lboost_fiber -lboost_date_time -lboost_filesystem -lboost_iostreams -lboost_serialization -lboost_timer -lboost_program_options
ifeq ($(findstring clang,$(CC)),clang)
WARNINGS += -Wno-c++11-narrowing -Wno-unused-command-line-argument
LDFLAGS += -lstdc++
else
WARNINGS += -Wmaybe-uninitialized
LDFLAGS += -lstdc++
endif

UMD_HOME = umd
UMD_VERSIM_HEADERS = $(BUDA_HOME)/$(TT_MODULES)/versim/
UMD_USER_ROOT = $(BUDA_HOME)

ifeq ($(EMULATION_DEVICE_EN), 1)
  TENSIX_EMULATION_ZEBU    	 ?= $(TENSIX_EMULATION_ROOT)/zebu
  TENSIX_EMULATION_ZCUI_WORK ?= $(TENSIX_EMULATION_ROOT)/targets/tensix_2x2_1dram_BH/zcui.work
  TENSIX_EMULATION_RUNDIR 	 ?= $(OUT)/rundir_zebu

  TENSIX_EMULATION_LDFLAGS += -L$(ZEBU_IP_ROOT)/lib -L$(ZEBU_ROOT)/lib -LDFLAGS "-g"
  TENSIX_EMULATION_LIBS +=  -lxtor_amba_master_svs -lZebuXtor -lZebu -lZebuZEMI3 -lZebuVpi \
                            -Wl,-rpath,$(ZEBU_IP_ROOT)/lib\
                            $(OUT)/zcui.work/zebu.work/xtor_amba_master_axi4_svs.so\
                            $(OUT)/zcui.work/zebu.work/tt_emu_cmd_xtor.so\
                            $(TENSIX_EMULATION_ZEBU)/lib/libtt_zebu_wrapper.so

  LDFLAGS +=  $(TENSIX_EMULATION_LDFLAGS) $(TENSIX_EMULATION_LIBS)

endif

ifndef PYTHON_VERSION
    PYTHON_VERSION := python$(shell python3 --version 2>&1 | cut -d " " -f 2 | cut -d "." -f 1,2)
    ifeq ($(PYTHON_VERSION),python3.8)
        $(info PYTHON_VERSION was not set previously, setting to installed python version 3.8)
    else ifeq ($(PYTHON_VERSION),python3.10)
        $(info PYTHON_VERSION was not set previously, setting to installed python version 3.10)
    else
        $(error PYTHON_VERSION was not set previously, and python3 installed version neither 3.8 nor 3.10)
    endif
else
    $(info PYTHON_VERSION was already set to $(PYTHON_VERSION))
endif

build: backend build_hw
ifeq ("$(HOST_ARCH)", "aarch64")
# Don't build RiscV FW on ARM. Supported toolchain only works on x86.
build_hw: gitinfo umd src_ops src_pipes src_verif src_perf_lib netlist loader runtime
else
build_hw: gitinfo umd build_fw src_ops src_pipes src_verif src_perf_lib netlist loader runtime
build_fw: gitinfo src/ckernels src/firmware
endif

unit_tests:
	@$(MAKE) \
	  loader/unit_tests UMD_VERSIM_STUB=0 \
	  netlist/unit_tests LOGGER_LEVEL=DISABLED \
	  src/net2pipe/unit_tests LOGGER_LEVEL=DISABLED \
	  src/pipegen2/unit_tests \

valgrind: $(VG_EXE)
	valgrind --show-leak-kinds=all \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=valgrind-out.txt $(VG_ARGS) $(VG_EXE)

src_ops: compile_trisc
src_pipes: src/net2pipe src/pipegen2 src/blobgen2
src_verif: verif/op_tests/tools/spm
src_perf_lib: perf_lib perf_lib/op_model
umd: umd_device

clean: src/ckernels/clean src/firmware/clean clean_umd
	rm -rf $(OUT)
clean_fw: src/firmware/clean
clean_umd: clean_umd_device
install: build
ifeq ($(PREFIX), $(OUT))
	@echo "To install you must set PREFIX, e.g."
	@echo ""
	@echo "  PREFIX=/usr CONFIG=release make install"
	@echo ""
	@exit 1
endif
	cp -r $(LIBDIR)/* $(PREFIX)/lib/
	cp -r $(INCDIR)/* $(PREFIX)/include/
	cp -r $(BINDIR)/* $(PREFIX)/bin/

package:
	tar czf spatial2_$(shell git rev-parse HEAD).tar.gz --exclude='*.d' --exclude='*.o' --exclude='*.a' --transform 's,^,spatial2_$(shell git rev-parse HEAD)/,' -C $(OUT) .

gitinfo:
	mkdir -p $(OUT)
# Initialization steps for building + running the Zebu emulator device
ifeq ($(EMULATION_DEVICE_EN),1)
	mkdir -p $(TENSIX_EMULATION_RUNDIR)
	cp -f $(TENSIX_EMULATION_ZEBU)/scripts/designFeatures $(TENSIX_EMULATION_RUNDIR)/designFeatures
	ln -sf $(TENSIX_EMULATION_ZCUI_WORK) $(OUT)/.
	ln -sf ../../build $(TENSIX_EMULATION_RUNDIR)/.
endif
	rm -f $(OUT)/.gitinfo
	@echo $(GIT_BRANCH) >> $(OUT)/.gitinfo
	@echo $(GIT_HASH) >> $(OUT)/.gitinfo

# These must be in dependency order (enforces no circular deps)
include docs/public/module.mk
include common/module.mk
include umd/device/module.mk
include src/ckernels/module.mk
include src/firmware/module.mk
#include python_env/module.mk

include model/module.mk
include ops/module.mk
include loader/module.mk
include runtime/module.mk
include src/pipegen2/module.mk
# Note that blobgen2 includes pipegen2 lib, so has to be included after pipegen2
include src/blobgen2/module.mk
include golden/module.mk
include netlist/module.mk
include net2hlks_lib/module.mk
include compile_trisc/module.mk
include perf_lib/module.mk
include perf_lib/op_model/module.mk
include dbd/server/lib/module.mk

# resume mainstream backend targets
include backend/module.mk
include src/net2pipe/module.mk
include src/net2hlks/module.mk
include verif/module.mk
include loader/tests/module.mk
include loader/unit_tests/module.mk
include runtime/tests/module.mk

#include python_api/module.mk
#include pybuda/module.mk
#include docs/public/module.mk
#include bg/module.mk
#include rbg/module.mk
include tb/llk_tb/overlay/module.mk
include tb/llk_tb/module.mk
include dbd/module.mk
include verif/op_tests/tools/spm/module.mk
include py_api/module.mk
include netlist_analyzer/module.mk

# all targets, added for convenience
all: backend build_hw compile_trisc/tests dbd docs/public eager_backend golden loader/tests \
     netlist_analyzer ops py_api runtime/tests src/net2hlks tb/llk_tb \
     verif verif/directed_tests verif/error_tests verif/graph_tests verif/op_tests verif/tm_tests \
     netlist/unit_tests src/net2pipe/unit_tests src/pipegen2/unit_tests
     # These are not working yet: dbdtests netlist/tests unit_tests
