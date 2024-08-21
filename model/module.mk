# Every variable in subdir must be prefixed with subdir (emulating a namespace)

INCLUDE_DIRS+=-Inetlist -Imodel -Imodel/ops -I. -Ithird_party/json/ -Icommon/model/ -Icommon

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  INCLUDE_DIRS+=-Isrc/firmware/riscv/wormhole
  INCLUDE_DIRS+=-Isrc/firmware/riscv/wormhole/wormhole_b0_defines
else
  INCLUDE_DIRS+=-Isrc/firmware/riscv/$(ARCH_NAME)
endif

ifeq ("$(ARCH_NAME)", "wormhole")
  INCLUDE_DIRS+=-Isrc/firmware/riscv/wormhole/wormhole_a0_defines
endif

MODEL_LIB = $(LIBDIR)/libmodel.a
MODEL_SRCS = \
	model/base_defs.cpp \
	model/buffer.cpp \
	model/dram.cpp \
	model/model.cpp \
	model/op.cpp \
	model/tensor.cpp \
	model/tile.cpp \
	model/tt_rnd_util.cpp \
	model/utils.cpp \
	model/data_format.cpp \
	model/simd_tile_ops.cpp \

ifeq ($(NO_DISTRIBUTED_EPOCH_TABLES),1)
	CXXFLAGS += $(addprefix -DNO_DISTRIBUTED_EPOCH_TABLES=, $(NO_DISTRIBUTED_EPOCH_TABLES))
endif

ifneq ("$(HOST_ARCH)", "x86_64")
# This file will not use AVX functions with ARM 
MODEL_SRCS += \
	model/tilize_untilize_api/tilize.cpp \
	model/tilize_untilize_api/host_untilize.cpp
else
#  Include all AVX tilizer files for x86. 
MODEL_SRCS += \
	$(wildcard model/tilize_untilize_api/*.cpp)
endif

MODEL_INCLUDES = $(INCLUDE_DIRS) -Ithird_party/json -Iumd

MODEL_OBJS = $(addprefix $(OBJDIR)/, $(MODEL_SRCS:.cpp=.o))
MODEL_DEPS = $(addprefix $(OBJDIR)/, $(MODEL_SRCS:.cpp=.d))

-include $(MODEL_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
model: $(MODEL_LIB)

# TODO: add src/firmware src/ckernel when they become real targets
$(MODEL_LIB): $(COMMON_LIB) $(PIPEGEN) $(UMD_DEVICE_LIB) $(MODEL_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(MODEL_OBJS)

.PRECIOUS: $(OBJDIR)/model/%.o
$(OBJDIR)/model/%.o: model/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(MODEL_INCLUDES) -c -o $@ $<

.PRECIOUS: $(OBJDIR)/model/ops/%.o
$(OBJDIR)/model/ops/%.o: model/ops/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(MODEL_INCLUDES) -c -o $@ $<