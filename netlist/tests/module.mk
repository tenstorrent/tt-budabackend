# Every variable in subdir must be prefixed with subdir (emulating a namespace)
NETLIST_TESTS = $(basename $(wildcard netlist/tests/test*.cpp))

NETLIST_TESTS_SRCS = $(addsuffix .cpp, $(NETLIST_TESTS))

NETLIST_TEST_INCLUDES = $(NETLIST_INCLUDES) -Inetlist/tests -Inetlist
NETLIST_TESTS_LDFLAGS = -ldevice -lmodel -lstdc++fs -lpthread -lyaml-cpp

NETLIST_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(NETLIST_TESTS_SRCS:.cpp=.o))
NETLIST_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(NETLIST_TESTS_SRCS:.cpp=.d))

-include $(NETLIST_TESTS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
netlist/tests: $(NETLIST_TESTS)  netlist/tests/unit_tests
netlist/tests/all: $(NETLIST_TESTS) netlist/tests/unit_tests
netlist/tests/%: $(TESTDIR)/netlist/tests/% ;

.PRECIOUS: $(TESTDIR)/netlist/tests/%
$(TESTDIR)/netlist/tests/%: $(OBJDIR)/netlist/tests/%.o $(MODEL_LIB) $(NETLIST_LIB) $(UMD_DEVICE_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NETLIST_TEST_INCLUDES) -o $@ $^ $(LDFLAGS) $(NETLIST_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/netlist/tests/%.o
$(OBJDIR)/netlist/tests/%.o: netlist/tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NETLIST_TEST_INCLUDES) -c -o $@ $<
