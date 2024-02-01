# Every variable in subdir must be prefixed with subdir (emulating a namespace)

LOADER_UNIT_TESTS_CFLAGS = $(CFLAGS) -Werror

# Get the list of unit test files
LOADER_UNIT_TESTS += $(basename $(wildcard loader/unit_tests/*.cpp))
LOADER_UNIT_TESTS_SRCS = $(addsuffix .cpp, $(LOADER_UNIT_TESTS))

# Include paths
LOADER_TEST_INCLUDES = $(LOADER_INCLUDES) -Iloader/unit_tests -Icompile_trisc -Iverif
LOADER_UNIT_TESTS_LDFLAGS = -ltt -ldevice -lstdc++fs -pthread -lruntime -lop_model -lyaml-cpp -lcommon -lhwloc -lgtest -lgtest_main

# Corresponding object files and dependency files
LOADER_UNIT_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(LOADER_UNIT_TESTS_SRCS:.cpp=.o))
LOADER_UNIT_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(LOADER_UNIT_TESTS_SRCS:.cpp=.d))

-include $(LOADER_UNIT_TESTS_DEPS)

# Libraries this target depends on
LOADER_UNIT_TESTS_LIB_DEPS = $(BACKEND_LIB) $(LOADER_LIB) $(VERIF_LIB)

# Rule to compile each object file
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(LOADER_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(LOADER_TEST_INCLUDES) -c $< -o $@

# Rule to link the final test binary
loader/unit_tests: $(TESTDIR)/loader/unit_tests/loader_unit_tests

.PHONY: $(TESTDIR)/loader/unit_tests/loader_unit_tests
$(TESTDIR)/loader/unit_tests/loader_unit_tests: $(LOADER_UNIT_TESTS_OBJS) $(LOADER_UNIT_TESTS_LIB_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(LOADER_UNIT_TESTS_CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LOADER_UNIT_TESTS_LDFLAGS)
