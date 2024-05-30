# Every variable in subdir must be prefixed with subdir (emulating a namespace)
PIPEGEN2_LIB_SRCS  = $(wildcard src/pipegen2/lib/src/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/client/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/data_flow_calculator/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/data_flow_calculator/prototype/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/device/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/device/l1/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/fork_join_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/pipe_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/rational_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/stream_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/stream_graph/ncrisc_creators/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/stream_graph/pipe_streams_creators/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/stream_graph/resources_allocators/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/graph_creator/stream_graph/stream_creators/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/io/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/data_flow/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/fork_join_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/pipe_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/rational_graph/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/rational_graph/nodes/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/rational_graph/pipes/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/rational_graph/pipes/direct/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/rational_graph/pipes/fork/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/rational_graph/pipes/join/*.cpp)
PIPEGEN2_LIB_SRCS += $(wildcard src/pipegen2/lib/src/model/stream_graph/*.cpp)
PIPEGEN2_LIB = $(LIBDIR)/libpipegen2.a

PIPEGEN2_OBJS = $(addprefix $(OBJDIR)/, $(PIPEGEN2_LIB_SRCS:.cpp=.o))
PIPEGEN2_DEPS = $(addprefix $(OBJDIR)/, $(PIPEGEN2_LIB_SRCS:.cpp=.d))

PIPEGEN2_INCLUDES = $(BASE_INCLUDES)

# Not necessary since whole budabackend root folder is already added to includes in main Makefile, but we should still
# specify our includes here in order to be explicit from the module makefile about what exactly we include.
PIPEGEN2_INCLUDES = \
	-Icommon \
	-Inetlist \
	-Iperf_lib \
	-Isrc/pipegen2/lib/inc \
	-Iumd \
	-Iutils \

# Including appropriate firmware definitions (stream_io_map.h etc).
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  PIPEGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole
  PIPEGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole/noc
else
  PIPEGEN2_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)
  PIPEGEN2_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)/noc
endif

# Include arch specific includes.
PIPEGEN2_INCLUDES += -Isrc/pipegen2/lib/arch_inc/$(ARCH_NAME)

-include $(PIPEGEN2_DEPS)

PERF_DUMP_LEVEL ?= 0
CFLAGS += $(addprefix -DPERF_DUMP_LEVEL=, $(PERF_DUMP_LEVEL))

.PRECIOUS: $(OBJDIR)/src/pipegen2/%.o
$(OBJDIR)/src/pipegen2/%.o: src/pipegen2/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(PIPEGEN2_INCLUDES) -c -o $@ $<

# Each module has a top level target as the entrypoint which must match the subdir name
src/pipegen2/lib: $(PIPEGEN2_LIB)

$(PIPEGEN2_LIB): $(PIPEGEN2_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(PIPEGEN2_OBJS)