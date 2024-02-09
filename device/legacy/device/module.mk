# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEVICE_LIB = $(LIBDIR)/libdevice.so
DEVICE_SRCS = \
	device/tt_device.cpp \
	device/tt_memory.cpp \
	device/tt_hexfile.cpp \
	device/tt_silicon_driver.cpp \
	device/tt_silicon_driver_common.cpp \
  device/tt_soc_descriptor.cpp \
  device/tt_cluster_descriptor.cpp \
  device/cpuset_lib.cpp

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_SRCS += device/wormhole/impl_device.cpp
else
  DEVICE_SRCS += device/$(ARCH_NAME)/impl_device.cpp
endif

ifeq ($(BACKEND_VERSIM_STUB),1)
DEVICE_SRCS += device/tt_versim_stub.cpp
else
DEVICE_SRCS += device/tt_versim_device.cpp
endif


DEVICE_OBJS = $(addprefix $(OBJDIR)/, $(DEVICE_SRCS:.cpp=.o))
DEVICE_DEPS = $(addprefix $(OBJDIR)/, $(DEVICE_SRCS:.cpp=.d))

DEVICE_INCLUDES=      	\
  -DFMT_HEADER_ONLY     \
  -I$(BUDA_HOME)/third_party/fmt

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_INCLUDES += -I$(BUDA_HOME)/device/wormhole/
else
  DEVICE_INCLUDES += -I$(BUDA_HOME)/device/$(ARCH_NAME)/
endif

ifeq ($(BACKEND_VERSIM_FULL_DUMP), 1)
ifneq ("$(ARCH_NAME)", "wormhole_b0")
  $(error "FPU wave dump only available for wormhole_b0")
endif

VERSIM_LIB = fpu_waves_lib
else
VERSIM_LIB = lib
endif

ifneq ($(BACKEND_VERSIM_STUB),1)
DEVICE_INCLUDES+=      	\
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/verilator/include         \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/verilator/include/vltstd  \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/yaml-cpp/include                                   \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/fc4sc/includes                                     \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tclap/include                                      \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/vendor/tenstorrent-repositories/range-v3/include          \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/hardware/tdma/tb/tile                                 \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/meta/$(ARCH_NAME)/instructions/inc                       \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/meta/$(ARCH_NAME)/types/inc                              \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/software/command_assembler/inc                        \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/common                                          \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core                                     \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/common                              \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/common/inc                          \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/monitors                            \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/t6ifc/versim-core/checkers                            \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/src/tvm/inc                                               \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers/usr_include                                               \
  -I$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/headers
endif

DEVICE_INCLUDES += -I$(BUDA_HOME)/loader

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_b0_defines
else
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)   
endif

ifeq ("$(ARCH_NAME)", "wormhole")
  DEVICE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_a0_defines 
endif

DEVICE_LDFLAGS = -Wl,-rpath,$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/$(VERSIM_LIB),-rpath,$(BUDA_HOME)/common_lib
ifdef DEVICE_VERSIM_INSTALL_ROOT
DEVICE_LDFLAGS += -Wl,-rpath,$(DEVICE_VERSIM_INSTALL_ROOT)/$(TT_MODULES)/versim/$(ARCH_NAME)/$(VERSIM_LIB),-rpath,$(DEVICE_VERSIM_INSTALL_ROOT)/common_lib
endif
DEVICE_LDFLAGS += \
	-lcommon -lhwloc
ifneq ($(BACKEND_VERSIM_STUB),1)
  DEVICE_LDFLAGS += \
	-L$(BUDA_HOME)/$(TT_MODULES)/versim/$(ARCH_NAME)/$(VERSIM_LIB) \
	-lz \
	-lyaml-cpp \
	-L$(BUDA_HOME)/common_lib \
	-lz \
	-lyaml-cpp \
	-l:libboost_system.so.1.65.1 \
	-l:libboost_filesystem.so.1.65.1 \
	-l:libz.so.1 \
	-l:libglog.so.0 \
	-l:libicudata.so.60 \
	-l:libicui18n.so.60 \
	-l:libicuuc.so.60 \
	-l:libboost_thread.so.1.65.1 \
	-l:libboost_regex.so.1.65.1 \
	-l:libsqlite3.so.0 \
	-lpthread \
	-latomic \
  -l:versim-core.so -l:libc_verilated_hw.so
endif

CLANG_WARNINGS := $(filter-out -Wmaybe-uninitialized,$(WARNINGS))
CLANG_WARNINGS += -Wsometimes-uninitialized
ifeq ("$(HOST_ARCH)", "aarch64")
DEVICE_CXX = /usr/bin/clang++
else
DEVICE_CXX = /usr/bin/clang++-6.0
endif
DEVICE_CXXFLAGS = -MMD $(CLANG_WARNINGS) -I$(BUDA_HOME)/. --std=c++17
ifeq ($(CONFIG), release)
DEVICE_CXXFLAGS += -O3
else ifeq ($(CONFIG), ci)
DEVICE_CXXFLAGS += -O3  # significantly smaller artifacts
else ifeq ($(CONFIG), assert)
DEVICE_CXXFLAGS += -O3 -g
else ifeq ($(CONFIG), asan)
DEVICE_CXXFLAGS += -O3 -g -fsanitize=address
else ifeq ($(CONFIG), ubsan)
DEVICE_CXXFLAGS += -O3 -g -fsanitize=undefined
else ifeq ($(CONFIG), debug)
DEVICE_CXXFLAGS += -O0 -g -DDEBUG
else
$(error Unknown value for CONFIG "$(CONFIG)")
endif

ifneq ($(filter "$(ARCH_NAME)","wormhole" "wormhole_b0"),)
  DEVICE_CXXFLAGS += -DEN_DRAM_ALIAS
  ifeq ($(DISABLE_ISSUE_3487_FIX), 1)
    DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
  endif
else
  DEVICE_CXXFLAGS += -DDISABLE_ISSUE_3487_FIX
endif

-include $(DEVICE_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
device: $(DEVICE_LIB) 

$(DEVICE_LIB): $(COMMON_LIB) $(DEVICE_OBJS)
	@mkdir -p $(LIBDIR)
	$(DEVICE_CXX) $(DEVICE_CXXFLAGS) $(SHARED_LIB_FLAGS) -o $(DEVICE_LIB) $^ $(LDFLAGS) $(DEVICE_LDFLAGS)

$(OBJDIR)/device/%.o: device/%.cpp
	@mkdir -p $(@D)
	$(DEVICE_CXX) $(DEVICE_CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEVICE_INCLUDES) -c -o $@ $<

