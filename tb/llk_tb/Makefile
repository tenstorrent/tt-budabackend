
ROOT?=$(shell git rev-parse --show-toplevel)

include $(ROOT)/infra/common.mk

STANDALONE?=1
VERSIM_RELEASE?=0
PROFILE?=0
RELEASE?=0

TT_MODULES=
ARCH_NAME ?= grayskull
HEADERS_RELEASE_DIR=$(ROOT)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers

ifndef OUT
OUT?=$(ROOT)/build/test/llk
endif

ifeq ($(STANDALONE),1)
HEADERS_BASE_DIR=$(HEADERS_RELEASE_DIR)
$(info "Running standalone build")
else
HEADERS_BASE_DIR=$(ROOT)
endif

ifeq ($(VERSIM_RELEASE),1)
$(info "Running release build")
endif

DEVICE_TYPE=0

ifeq ($(BACKEND_VERSIM_FULL_DUMP), 1)
ifneq ("$(ARCH_NAME)", "wormhole_b0")
  $(error "FPU wave dump only available for wormhole_b0")
endif

VERSIM_LIB_DIR            = $(ROOT)/$(TT_MODULES)/versim/$(ARCH_NAME)/fpu_waves_lib
else
VERSIM_LIB_DIR            = $(ROOT)/$(TT_MODULES)/versim/$(ARCH_NAME)/lib
endif


VERSIM_LIB                = -l:versim-core.so -l:libc_verilated_hw.so
ZLIB                      = -lz
GLOG                      = -lglog

MAKEFLAGS                 = -j8

$(info "HEADERS_BASE_DIR $(HEADERS_BASE_DIR)")
INCLUDE_DIRS=                                                                   \
  ./inc                                                                         \
  ./tests/                                                                      \
  ./tests/lib/inc                                                               \
  $(HEADERS_BASE_DIR)/vendor/tenstorrent-repositories/verilator/include         \
  $(HEADERS_BASE_DIR)/vendor/tenstorrent-repositories/verilator/include/vltstd  \
  $(HEADERS_BASE_DIR)/vendor/yaml-cpp/include                                   \
  $(HEADERS_BASE_DIR)/vendor/fc4sc/includes                                     \
  $(HEADERS_BASE_DIR)/vendor/tclap/include                                      \
  $(HEADERS_BASE_DIR)/vendor/tenstorrent-repositories/range-v3/include          \
  $(HEADERS_BASE_DIR)/src/hardware/tdma/tb/tile                                 \
  $(HEADERS_BASE_DIR)/src/meta/$(ARCH_NAME)/instructions/inc                       \
  $(HEADERS_BASE_DIR)/src/meta/$(ARCH_NAME)/types/inc                              \
  $(HEADERS_BASE_DIR)/src/software/command_assembler/inc                        \
  $(HEADERS_BASE_DIR)/src/t6ifc/common                                          \
  $(HEADERS_BASE_DIR)/src/t6ifc/versim-core                                     \
  $(HEADERS_BASE_DIR)/src/t6ifc/versim-core/common                              \
  $(HEADERS_BASE_DIR)/src/t6ifc/versim-core/common/inc                          \
  $(HEADERS_BASE_DIR)/src/t6ifc/versim-core/monitors                            \
  $(HEADERS_BASE_DIR)/src/t6ifc/versim-core/checkers                            \
  $(HEADERS_BASE_DIR)/src/tvm/inc                                               \
  $(HEADERS_BASE_DIR)/usr_include                                               \
  $(HEADERS_BASE_DIR)

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  INCLUDE_DIRS += $(ROOT)/src/firmware/riscv/wormhole
  INCLUDE_DIRS += $(ROOT)/src/firmware/riscv/wormhole/noc
  INCLUDE_DIRS += $(ROOT)/src/firmware/riscv/wormhole/wormhole_b0_defines
else
  INCLUDE_DIRS += $(ROOT)/src/firmware/riscv/$(ARCH_NAME)
  INCLUDE_DIRS += $(ROOT)/src/firmware/riscv/$(ARCH_NAME)/noc
endif

ifeq ("$(ARCH_NAME)", "wormhole")
  INCLUDE_DIRS += $(ROOT)/src/firmware/riscv/wormhole/wormhole_a0_defines 
endif


INCLUDE_FLAGS := $(addprefix -I,$(INCLUDE_DIRS))

CPP_FILES =           \
	main.cpp
CPP_FILES += $(wildcard src/*.cpp)
CPP_FILES += $(wildcard src/utils/*.cpp)
CPP_FILES += $(wildcard tests/**/*.cpp)
CPP_FILES += $(wildcard tests/lib/src/*.cpp)

OBJS := $(CPP_FILES:%=$(OUT)/%.o)
DEPS := $(OBJS:.o=.d)

EXT_LIBS = \
 -l:libboost_system.so.1.65.1 \
 -l:libboost_filesystem.so.1.65.1 \
 -l:libz.so.1 \
 -l:libglog.so.0 \
 -l:libicudata.so.60 \
 -l:libicui18n.so.60 \
 -l:libicuuc.so.60 \
 -l:libboost_thread.so.1.65.1 \
 -l:libboost_regex.so.1.65.1 \
 -l:libsqlite3.so.0

CXX=/usr/bin/clang++-6.0
CXXFLAGS= -MD -std=c++17 $(INCLUDE_FLAGS) 
#CXXLIBS=-lpthread -lboost_system -lboost_filesystem -latomic -Wl,--no-whole-archive $(ZLIB) $(GLOG) -Wl,-rpath,$(VERSIM_LIB_DIR) -L$(VERSIM_LIB_DIR) $(VERSIM_LIB)
CXXLIBS= -lpthread -lstdc++fs -latomic -Wl,-rpath,$(VERSIM_LIB_DIR) -L$(VERSIM_LIB_DIR) $(VERSIM_LIB) -Wl,-rpath,$(ROOT)/common_lib -L$(ROOT)/common_lib $(EXT_LIBS) 
ifeq ($(PROFILE), 1) 
  CXXFLAGS += -pg
endif

ifeq ($(RELEASE), 1)
  CXXFLAGS += -Ofast
else 
  CXXFLAGS += -g -fno-limit-debug-info
endif
.PHONY: all
all: $(OUT) $(OUT)/llk_tb

$(OUT):
	mkdir -p $(OUT)

$(OUT)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT)/llk_tb: $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(CXXLIBS)
ifeq ($(VERSIM_RELEASE),1)
	$(info "Running release headers")
	if test -d $(HEADERS_RELEASE_DIR); then rm -rfd $(HEADERS_RELEASE_DIR); fi
	mkdir -p $(HEADERS_RELEASE_DIR)
	find . -name *.d | xargs sed '1d' | tr -d '\' | xargs -I@ bash -c 'cp --parent `realpath @` $(HEADERS_RELEASE_DIR)'
	mv $(HEADERS_RELEASE_DIR)$(ROOT)/* $(HEADERS_RELEASE_DIR)
	rm -rfd $(HEADERS_RELEASE_DIR)/localhome
endif

clean:
	rm -f *.d
	rm -f versim
	rm -f *.o
	rm -rfd out

