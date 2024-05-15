#Every variable in subdir must be prefixed with subdir(emulating a namespace)
VERIF_ERROR_TESTS += $(basename $(wildcard verif/error_tests/*.cpp))

VERIF_ERROR_TESTS_SRCS = $(addsuffix .cpp, $(VERIF_ERROR_TESTS))

VERIF_ERROR_TESTS_INCLUDES = $(GOLDEN_INCLUDES) $(VERIF_INCLUDES) $(NETLIST_INCLUDES) $(LOADER_INCLUDES) $(MODEL_INCLUDES) $(COMPILE_TRISC_INCLUDES) -Iverif/error_tests -Iverif 
VERIF_ERROR_TESTS_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp


VERIF_ERROR_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_ERROR_TESTS_SRCS:.cpp=.o))
VERIF_ERROR_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_ERROR_TESTS_SRCS:.cpp=.d))

-include $(VERIF_ERROR_TESTS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
verif/error_tests: $(VERIF_ERROR_TESTS) 
verif/error_tests/all: $(VERIF_ERROR_TESTS)
verif/error_tests/%: $(TESTDIR)/verif/error_tests/%;

verif/error_tests/clean:
	@echo $(VERIF_ERROR_TESTS)
	rm -f test_gen*.cpp 

.PHONY: verif/error_tests/print
verif/error_tests/print:
	@echo $(VERIF_ERROR_TESTS_SRCS)

# FOR NOW: FIXME: We move the generated op tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/verif/error_tests/%
$(TESTDIR)/verif/error_tests/%: $(OBJDIR)/verif/error_tests/%.o $(BACKEND_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_ERROR_TESTS_INCLUDES) -o $@ $^ $(LDFLAGS) $(VERIF_ERROR_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/verif/error_tests/%.o
$(OBJDIR)/verif/error_tests/%.o: verif/error_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_ERROR_TESTS_INCLUDES) -c -o $@ $< 

