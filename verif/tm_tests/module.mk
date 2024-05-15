#Every variable in subdir must be prefixed with subdir(emulating a namespace)
VERIF_TM_TESTS += $(basename $(wildcard verif/tm_tests/test_tm_main.cpp))

VERIF_TM_TESTS_SRCS = $(addsuffix .cpp, $(VERIF_TM_TESTS))

VERIF_TM_TESTS_INCLUDES = $(GOLDEN_INCLUDES) $(VERIF_INCLUDES) $(NETLIST_INCLUDES) $(LOADER_INCLUDES) $(MODEL_INCLUDES) $(COMPILE_TRISC_INCLUDES) -Iverif/tm_tests -Iverif 
VERIF_TM_TESTS_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp


VERIF_TM_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_TM_TESTS_SRCS:.cpp=.o))
VERIF_TM_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_TM_TESTS_SRCS:.cpp=.d))

-include $(VERIF_TM_TESTS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
verif/tm_tests: $(VERIF_TM_TESTS) 
verif/tm_tests/all: $(VERIF_TM_TESTS)
verif/tm_tests/%: $(TESTDIR)/verif/tm_tests/%;

verif/tm_tests/clean:
	@echo $(VERIF_TM_TESTS)
	rm -f test_gen*.cpp 

.PHONY: verif/tm_tests/print
verif/tm_tests/print:
	@echo $(VERIF_TM_TESTS_SRCS)

# FOR NOW: FIXME: We move the generated op tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/verif/tm_tests/%
$(TESTDIR)/verif/tm_tests/%: $(OBJDIR)/verif/tm_tests/%.o $(BACKEND_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_TM_TESTS_INCLUDES) -o $@ $^ $(LDFLAGS) $(VERIF_TM_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/verif/tm_tests/%.o
$(OBJDIR)/verif/tm_tests/%.o: verif/tm_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_TM_TESTS_INCLUDES) -c -o $@ $< 

