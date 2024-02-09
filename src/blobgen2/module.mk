BLOBGEN2_BASE_DIR = src/blobgen2

# Every variable in subdir must be prefixed with subdir (emulating a namespace)
BLOBGEN2_SRCS =  $(wildcard $(BLOBGEN2_BASE_DIR)/src/*.cpp)
BLOBGEN2_SRCS += $(wildcard $(BLOBGEN2_BASE_DIR)/src/blob_graph_creator/*.cpp)
BLOBGEN2_SRCS += $(wildcard $(BLOBGEN2_BASE_DIR)/src/io/*.cpp)
BLOBGEN2_SRCS += $(wildcard $(BLOBGEN2_BASE_DIR)/src/model/*.cpp)
BLOBGEN2_SRCS += $(wildcard $(BLOBGEN2_BASE_DIR)/src/model/stream_graph/*.cpp) # TODO: Revert to pipegen stream_graph once it is completed and aligned.

BLOBGEN2 = $(BINDIR)/blobgen2
BLOBGEN2_OBJDIR = $(OBJDIR)/src/blobgen2

BLOBGEN2_OBJS = $(addprefix $(OBJDIR)/, $(BLOBGEN2_SRCS:.cpp=.o))
BLOBGEN2_DEPS = $(addprefix $(OBJDIR)/, $(BLOBGEN2_SRCS:.cpp=.d))

BLOBGEN2_INCLUDES = \
	-I$(BLOBGEN2_BASE_DIR)/inc \
	-I$(YAML_PATH)
#	-Isrc/pipegen2/inc/ \ # TODO: Revert to pipegen stream_graph

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole/noc
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole/wormhole_b0_defines
else
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)/noc
endif

ifeq ("$(ARCH_NAME)", "wormhole")
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole/wormhole_a0_defines
endif

BLOBGEN2_LDFLAGS = -lyaml-cpp -lm

-include $(BLOBGEN2_DEPS)

PERF_DUMP_LEVEL ?= 0
CFLAGS += $(addprefix -DPERF_DUMP_LEVEL=, $(PERF_DUMP_LEVEL))

# Each module has a top level target as the entrypoint which must match the subdir name
$(BLOBGEN2_BASE_DIR)/: $(BLOBGEN2_BASE_DIR)

# Each module has a top level target as the entrypoint which must match the subdir name
$(BLOBGEN2_BASE_DIR): $(BLOBGEN2)

$(BLOBGEN2): $(BLOBGEN2_OBJS)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(BLOBGEN2_LDFLAGS)

clean_blobgen2_test:
	echo "Removing $(BLOBGEN2_BASE_DIR)/test/inputs/*/out_blob.yaml"
	@find $(BLOBGEN2_BASE_DIR)/test/inputs -name out_blob.yaml -exec rm {} \;
	echo "Removing tt_build/blobgen2"
	@rm -rf tt_build/blobgen2

clean_blobgen2: clean_blobgen2_test
	echo "Removing $(BLOBGEN2)"
	@rm -rf $(BLOBGEN2)
	echo "Removing $(BLOBGEN2_OBJDIR)"
	@rm -rf $(BLOBGEN2_OBJDIR)

.PRECIOUS: $(BLOBGEN2_OBJDIR)/%.o
$(BLOBGEN2_OBJDIR)/%.o: $(BLOBGEN2_BASE_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(BLOBGEN2_INCLUDES) -c -o $@ $<