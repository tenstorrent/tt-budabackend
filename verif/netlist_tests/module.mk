#Every variable in subdir must be prefixed with subdir(emulating a namespace)
VERIF_GOLDEN_TESTS += $(basename $(wildcard verif/netlist_tests/*.cpp))

VERIF_GOLDEN_TESTS_SRCS = $(addsuffix .cpp, $(VERIF_GOLDEN_TESTS))

VERIF_GOLDEN_TESTS_INCLUDES = $(LOADER_INCLUDES) $(VERIF_INCLUDES) $(GOLDEN_INCLUDES) $(MODEL2_INCLUDES) $(NETLIST_INCLUDES) $(MODEL_INCLUDES) $(COREMODEL_INCLUDES) -Iverif/netlist_tests -Iverif 
VERIF_GOLDEN_TESTS_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp -lcommon -lhwloc $(COREMODEL_SPARTA_LIBS) -lzmq


VERIF_GOLDEN_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_GOLDEN_TESTS_SRCS:.cpp=.o))
VERIF_GOLDEN_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_GOLDEN_TESTS_SRCS:.cpp=.d))

-include $(VERIF_GOLDEN_TESTS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
verif/netlist_tests: build_hw $(VERIF_GOLDEN_TESTS) 
verif/netlist_tests/all: build_hw $(VERIF_GOLDEN_TESTS)
verif/netlist_tests/%: build_hw $(TESTDIR)/verif/netlist_tests/%;

verif/netlist_tests/clean:
	@echo $(VERIF_GOLDEN_TESTS)
	rm -f test_gen*.cpp 

.PHONY: verif/netlist_tests/print
verif/netlist_tests/print:
	@echo $(VERIF_GOLDEN_TESTS_SRCS)

# FOR NOW: FIXME: We move the generated op tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/verif/netlist_tests/%
$(TESTDIR)/verif/netlist_tests/%: $(OBJDIR)/verif/netlist_tests/%.o $(BACKEND_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_GOLDEN_TESTS_INCLUDES) -o $@ $^ $(LDFLAGS) $(VERIF_GOLDEN_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/verif/netlist_tests/%.o
$(OBJDIR)/verif/netlist_tests/%.o: verif/netlist_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_GOLDEN_TESTS_INCLUDES) -c -o $@ $< 

