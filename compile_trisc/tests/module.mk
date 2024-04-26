# Every variable in subdir must be prefixed with subdir (emulating a namespace)
COMPILE_TRISC_TESTS = \
	compile_trisc/tests/execute_trisc_compile

COMPILE_TRISC_TESTS_SRCS = $(addsuffix .cpp, $(COMPILE_TRISC_TESTS))

COMPILE_TRISC_TEST_INCLUDES = $(MODEL_INCLUDES) $(NETLIST_INCLUDES) $(COMPILE_TRISC_INCLUDES) -Icompile_trisc/tests -Icompile_trisc
COMPILE_TRISC_TESTS_LDFLAGS = -ltt -ldevice -lpthread -lstdc++fs -lcompile_trisc -lnetlist -lyaml-cpp -lmodel 

COMPILE_TRISC_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(COMPILE_TRISC_TESTS_SRCS:.cpp=.o))
COMPILE_TRISC_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(COMPILE_TRISC_TESTS_SRCS:.cpp=.d))

-include $(COMPILE_TRISC_TESTS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
compile_trisc/tests: $(COMPILE_TRISC_TESTS)
compile_trisc/tests/all: $(COMPILE_TRISC_TESTS)
compile_trisc/tests/%: $(TESTDIR)/compile_trisc/tests/% ;

.PRECIOUS: $(TESTDIR)/compile_trisc/tests/%
$(TESTDIR)/compile_trisc/tests/%: $(OBJDIR)/compile_trisc/tests/%.o  $(OP_LIB) $(NETLIST_LIB) $(MODEL_LIB) $(COMPILE_TRISC_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(COMPILE_TRISC_TEST_INCLUDES) -o $@ $^ $(LDFLAGS) $(COMPILE_TRISC_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/compile_trisc/tests/%.o
$(OBJDIR)/compile_trisc/tests/%.o: compile_trisc/tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(COMPILE_TRISC_TEST_INCLUDES) -c -o $@ $<
