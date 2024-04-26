#Every variable in subdir must be prefixed with subdir(emulating a namespace)
VERIF_GRAPH_TESTS += $(basename $(wildcard verif/graph_tests/*.cpp))

VERIF_GRAPH_TESTS_SRCS = $(addsuffix .cpp, $(VERIF_GRAPH_TESTS))

VERIF_GRAPH_TESTS_INCLUDES = $(GOLDEN_INCLUDES) $(VERIF_INCLUDES) $(NETLIST_INCLUDES) $(LOADER_INCLUDES) $(MODEL_INCLUDES) $(COMPILE_TRISC_INCLUDES) -Iverif/graph_tests -Iverif 
VERIF_GRAPH_TESTS_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp


VERIF_GRAPH_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_GRAPH_TESTS_SRCS:.cpp=.o))
VERIF_GRAPH_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_GRAPH_TESTS_SRCS:.cpp=.d))

-include $(VERIF_GRAPH_TESTS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
verif/graph_tests: $(VERIF_GRAPH_TESTS) 
verif/graph_tests/all: $(VERIF_GRAPH_TESTS)
verif/graph_tests/%: $(TESTDIR)/verif/graph_tests/%;

verif/graph_tests/clean:
	@echo $(VERIF_GRAPH_TESTS)
	rm -f test_gen*.cpp 

.PHONY: verif/graph_tests/print
verif/graph_tests/print:
	@echo $(VERIF_GRAPH_TESTS_SRCS)

# FOR NOW: FIXME: We move the generated graph tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/verif/graph_tests/%
$(TESTDIR)/verif/graph_tests/%: $(OBJDIR)/verif/graph_tests/%.o $(BACKEND_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_GRAPH_TESTS_INCLUDES) -o $@ $^ $(LDFLAGS) $(VERIF_GRAPH_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/verif/graph_tests/%.o
$(OBJDIR)/verif/graph_tests/%.o: verif/graph_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_GRAPH_TESTS_INCLUDES) -c -o $@ $< 

