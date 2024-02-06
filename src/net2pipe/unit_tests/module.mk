# Every variable in subdir must be prefixed with subdir (emulating a namespace)
NET2PIPE_UNIT_TESTS_SRCS += $(wildcard src/net2pipe/unit_tests/*.cpp)

NET2PIPE_UNIT_TESTS_BUILD_DIR = $(UTSDIR)/src/net2pipe
NET2PIPE_UNIT_TESTS_BIN_DIR = $(NET2PIPE_UNIT_TESTS_BUILD_DIR)/bin
NET2PIPE_UNIT_TESTS_OBJ_DIR = $(NET2PIPE_UNIT_TESTS_BUILD_DIR)/obj
NET2PIPE_UNIT_TESTS_BIN = $(NET2PIPE_UNIT_TESTS_BIN_DIR)/net2pipe_unit_tests

NET2PIPE_UNIT_TESTS_OBJS += $(addprefix $(NET2PIPE_UNIT_TESTS_OBJ_DIR)/, $(NET2PIPE_UNIT_TESTS_SRCS:.cpp=.o))
NET2PIPE_UNIT_TESTS_LDFLAGS = -lgtest -lop_model

NET2PIPE_OBJS_NOT_MAIN = $(foreach f,$(ALL_NET2PIPE_DEPENDENCY_OBJS),$(if $(findstring main.o,$(f)),,$(f)))

.PRECIOUS: $(NET2PIPE_UNIT_TESTS_BIN)
$(NET2PIPE_UNIT_TESTS_BIN): $(NET2PIPE_UNIT_TESTS_OBJS) $(NET2PIPE_OBJS_NOT_MAIN) $(PERF_LIB) $(GOLDEN_LIB) $(BACKEND_LIB)
	@echo "Building: $(NET2PIPE_UNIT_TESTS_BIN)"
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(NET2PIPE_LDFLAGS) $(NET2PIPE_UNIT_TESTS_LDFLAGS)

.PRECIOUS: $(NET2PIPE_UNIT_TESTS_OBJ_DIR)/src/net2pipe/unit_tests/%.o
$(NET2PIPE_UNIT_TESTS_OBJ_DIR)/src/net2pipe/unit_tests/%.o: src/net2pipe/unit_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NET2PIPE_INCLUDES) -c -o $@ $<

.PHONY: src_net2pipe_unit_tests_run_only src/net2pipe/unit_tests

src_net2pipe_unit_tests_run_only:
	@echo "Running: $(NET2PIPE_UNIT_TESTS_BIN)"
	@$(NET2PIPE_UNIT_TESTS_BIN)

src/net2pipe/unit_tests: $(NET2PIPE_UNIT_TESTS_BIN)
ifndef SKIP_UNIT_TESTS_RUN
	@$(MAKE) src_net2pipe_unit_tests_run_only
endif