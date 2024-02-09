#Every variable in subdir must be prefixed with subdir(emulating a namespace)
VERIF_DIRECTED_TESTS += $(basename $(wildcard verif/directed_tests/*.cpp))

VERIF_DIRECTED_TESTS_SRCS = $(addsuffix .cpp, $(VERIF_DIRECTED_TESTS))

VERIF_DIRECTED_TESTS_INCLUDES = $(VERIF_INCLUDES) $(GOLDEN_INCLUDES) $(MODEL2_INCLUDES) $(NETLIST_INCLUDES) $(MODEL_INCLUDES) $(COREMODEL_INCLUDES) -Iverif/directed_tests -Iverif 
VERIF_DIRECTED_TESTS_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp -lcommon -lhwloc $(COREMODEL_SPARTA_LIBS)


VERIF_DIRECTED_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_DIRECTED_TESTS_SRCS:.cpp=.o))
VERIF_DIRECTED_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_DIRECTED_TESTS_SRCS:.cpp=.d))

-include $(VERIF_DIRECTED_TESTS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
verif/directed_tests: build_hw $(VERIF_DIRECTED_TESTS) 
verif/directed_tests/all: build_hw $(VERIF_DIRECTED_TESTS)
verif/directed_tests/%: build_hw $(TESTDIR)/verif/directed_tests/%;

verif/directed_tests/clean:
	@echo $(VERIF_DIRECTED_TESTS)
	rm -f test_gen*.cpp 

.PHONY: verif/directed_tests/print
verif/directed_tests/print:
	@echo $(VERIF_DIRECTED_TESTS_SRCS)

# FOR NOW: FIXME: We move the generated op tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/verif/directed_tests/%
$(TESTDIR)/verif/directed_tests/%: $(OBJDIR)/verif/directed_tests/%.o $(BACKEND_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_DIRECTED_TESTS_INCLUDES) -o $@ $^ $(LDFLAGS) $(VERIF_DIRECTED_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/verif/directed_tests/%.o
$(OBJDIR)/verif/directed_tests/%.o: verif/directed_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_DIRECTED_TESTS_INCLUDES) -c -o $@ $< 

