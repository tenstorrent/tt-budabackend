# Intentionally kept to a minimum to mimic stand-alone backend release experience

RUNTIME_TESTS_CFLAGS = $(CFLAGS) -Werror

RUNTIME_TESTS += $(basename $(wildcard runtime/tests/*.cpp))
RUNTIME_TESTS_SRCS = $(addsuffix .cpp, $(RUNTIME_TESTS))

RUNTIME_TEST_INCLUDES = $(RUNTIME_INCLUDES) $(RUNTIME_INCLUDES) -Iruntime/tests
RUNTIME_TESTS_LDFLAGS = -ltt -ldevice  # DO NOT ADD!!! ping @tzhou @asaigal

RUNTIME_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(RUNTIME_TESTS_SRCS:.cpp=.o))
RUNTIME_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(RUNTIME_TESTS_SRCS:.cpp=.d))

-include $(RUNTIME_TESTS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
runtime/tests: $(RUNTIME_TESTS)
runtime/tests/all: $(RUNTIME_TESTS)
runtime/tests/%: $(TESTDIR)/runtime/tests/% ;

.PRECIOUS: $(TESTDIR)/runtime/tests/%
$(TESTDIR)/runtime/tests/%: $(OBJDIR)/runtime/tests/%.o $(BACKEND_LIB)
	@mkdir -p $(@D)
	$(CXX) $(RUNTIME_TESTS_CFLAGS) $(CXXFLAGS) $(RUNTIME_TEST_INCLUDES) -o $@ $^ $(LDFLAGS) $(RUNTIME_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/runtime/tests/%.o
$(OBJDIR)/runtime/tests/%.o: runtime/tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(RUNTIME_TESTS_CFLAGS) $(CXXFLAGS) $(RUNTIME_TEST_INCLUDES) -c -o $@ $<
