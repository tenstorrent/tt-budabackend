# Every variable in subdir must be prefixed with subdir (emulating a namespace)
NETLIST_ANALYZER_TESTS = $(basename $(wildcard netlist_analyzer/tests/*.cpp))

NETLIST_ANALYZER_TEST_SRCS = $(addsuffix .cpp, $(NETLIST_ANALYZER_TESTS))

NETLIST_ANALYZER_TEST_INCLUDES = $(NETLIST_ANALYZER_INCLUDES) -Inetlist_analyzer/tests -Inetlist $(VERIF_INCLUDES)
NETLIST_ANALYZER_TEST_LDFLAGS = -ltt -lnetlist_analyzer -lanalyzer -lstdc++fs -lpthread -lyaml-cpp -lcommon -lhwloc -lops -lverif

NETLIST_ANALYZER_TEST_OBJS = $(addprefix $(OBJDIR)/, $(NETLIST_ANALYZER_TEST_SRCS:.cpp=.o))
NETLIST_ANALYZER_TEST_DEPS = $(addprefix $(OBJDIR)/, $(NETLIST_ANALYZER_TEST_SRCS:.cpp=.d))

-include $(NETLIST_ANALYZER_TEST_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
netlist_analyzer/tests: backend netlist_analyzer verif $(NETLIST_ANALYZER_TESTS) 
netlist_analyzer/tests/all: backend netlist_analyzer verif $(NETLIST_ANALYZER_TESTS)
netlist_analyzer/tests/%:  backend netlist_analyzer verif $(TESTDIR)/netlist_analyzer/tests/%;

.PHONY: netlist_analyzer/tests/print
netlist_analyzer/tests/print:
	@echo $(NETLIST_ANALYZER_TEST_SRCS)

.PRECIOUS: $(TESTDIR)/netlist_analyzer/tests/%
$(TESTDIR)/netlist_analyzer/tests/%: $(OBJDIR)/netlist_analyzer/tests/%.o $(ANALYZER_LIB) $(NETLIST_ANALYZER_LIB) $(BACKEND_LIB) $(VERIF_LIB) 
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NETLIST_ANALYZER_TEST_INCLUDES) -o $@ $^ $(LDFLAGS) $(NETLIST_ANALYZER_TEST_LDFLAGS)

.PRECIOUS: $(OBJDIR)/netlist_analyzer/tests/%.o
$(OBJDIR)/netlist_analyzer/tests/%.o: netlist_analyzer/tests/%.cpp 
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NETLIST_ANALYZER_TEST_INCLUDES) -c -o $@ $<
