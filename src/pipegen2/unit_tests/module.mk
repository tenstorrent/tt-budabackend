# Every variable in subdir must be prefixed with subdir (emulating a namespace)
PIPEGEN2_UNIT_TESTS_SRCS  = $(wildcard src/pipegen2/unit_tests/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/data_flow_calculator/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/device/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/graph_creator/stream_graph/ncrisc_creators/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/io/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/mocks/device/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/test_utils/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/model/data_flow/*.cpp)
PIPEGEN2_UNIT_TESTS_SRCS += $(wildcard src/pipegen2/unit_tests/model/pipe_graph/*.cpp)

PIPEGEN2_UNIT_TESTS_BUILD_DIR = $(UTSDIR)/src/pipegen2
PIPEGEN2_UNIT_TESTS_BIN_DIR   = $(PIPEGEN2_UNIT_TESTS_BUILD_DIR)/bin
PIPEGEN2_UNIT_TESTS_OBJ_DIR   = $(PIPEGEN2_UNIT_TESTS_BUILD_DIR)/obj
PIPEGEN2_UNIT_TESTS_BIN       = $(PIPEGEN2_UNIT_TESTS_BIN_DIR)/pipegen2_unit_tests

PIPEGEN2_UNIT_TESTS_OBJS += $(addprefix $(PIPEGEN2_UNIT_TESTS_OBJ_DIR)/, $(PIPEGEN2_UNIT_TESTS_SRCS:.cpp=.o))

# Pipegen2 needs common and device libs. libdevice.so links as shared lib, while libcommon.a may be specified here as an
# -l flag, or later specified as a static lib path to CXX.
PIPEGEN2_UNIT_TESTS_LDLIBS = -lgtest -lgmock -lpthread -ldevice -lyaml-cpp -lm

PIPEGEN2_UNIT_TESTS_INCLUDES = \
	-Iumd \
	-Isrc/pipegen2/lib \
	-Isrc/pipegen2/unit_tests

$(PIPEGEN2_UNIT_TESTS_BIN): $(PIPEGEN2_UNIT_TESTS_OBJS) $(PIPEGEN2_LIB) $(COMMON_LIB) $(UMD_DEVICE_LIB)
	@echo "Building: $(PIPEGEN2_UNIT_TESTS_BIN)"
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(PIPEGEN2_UNIT_TESTS_LDLIBS)

.PRECIOUS: $(PIPEGEN2_UNIT_TESTS_OBJ_DIR)/src/pipegen2/unit_tests/%.o
$(PIPEGEN2_UNIT_TESTS_OBJ_DIR)/src/pipegen2/unit_tests/%.o: src/pipegen2/unit_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(PIPEGEN2_INCLUDES) $(PIPEGEN2_UNIT_TESTS_INCLUDES) -c -o $@ $<

.PHONY: src_pipegen2_unit_tests_run_only src/pipegen2/unit_tests

src_pipegen2_unit_tests_run_only:
	@echo "Running: $(PIPEGEN2_UNIT_TESTS_BIN)"
	@$(PIPEGEN2_UNIT_TESTS_BIN)

src/pipegen2/unit_tests: $(PIPEGEN2_UNIT_TESTS_BIN)
ifndef SKIP_UNIT_TESTS_RUN
	@$(MAKE) src_pipegen2_unit_tests_run_only
endif