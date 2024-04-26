# Every variable in subdir must be prefixed with subdir (emulating a namespace)

OP_MODEL_UNIT_TESTS_INCLUDES = $(COMMON_INCLUDES) $(NETLIST_INCLUDES) $(OP_MODEL_INCLUDES) $(OP_MODEL_ESTIMATE_EVALUATION_INCLUDES) -Iperf_lib/op_model/unit_tests
OP_MODEL_UNIT_TESTS_LDFLAGS = -L$(BUDA_HOME) $(COMMON_LDFLAGS) -lcommon $(NETLIST_LDFLAGS) -lnetlist $(OP_MODEL_LDFLAGS) -lop_model $(OP_MODEL_ESTIMATE_EVALUATION_LDFLAGS) -lop_model_estimate_evaluation -lgtest -lgtest_main -pthread

OP_MODEL_UNIT_TESTS_CFLAGS = $(CFLAGS) -Werror -Wall

OP_MODEL_UNIT_TESTS += $(basename $(wildcard perf_lib/op_model/unit_tests/*.cpp))
OP_MODEL_UNIT_TESTS_SRCS = $(addsuffix .cpp, $(OP_MODEL_UNIT_TESTS))

OP_MODEL_UNIT_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(OP_MODEL_UNIT_TESTS_SRCS:.cpp=.o))
OP_MODEL_UNIT_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(OP_MODEL_UNIT_TESTS_SRCS:.cpp=.d))

-include $(OP_MODEL_UNIT_TESTS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
perf_lib/op_model/unit_tests: $(TESTDIR)/op_model/unit_tests/op_model_unit_tests

.PHONY: $(TESTDIR)/op_model/unit_tests/op_model_unit_tests
$(TESTDIR)/op_model/unit_tests/op_model_unit_tests: $(COMMON_LIB) $(NETLIST_LIB) $(OP_MODEL_LIB) $(OP_MODEL_ESTIMATE_EVALUATION_LIB)
	@mkdir -p $(@D)
	$(CXX) $(OP_MODEL_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(OP_MODEL_UNIT_TESTS_INCLUDES) $(OP_MODEL_UNIT_TESTS_SRCS) -o $@ $^ $(LDFLAGS) $(OP_MODEL_UNIT_TESTS_LDFLAGS)
