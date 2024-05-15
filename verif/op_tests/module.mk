#Every variable in subdir must be prefixed with subdir(emulating a namespace)
VERIF_OP_TESTS += $(basename $(wildcard verif/op_tests/test_op.cpp))

VERIF_OP_TESTS_SRCS = $(addsuffix .cpp, $(VERIF_OP_TESTS))

VERIF_OP_TESTS_INCLUDES = $(GOLDEN_INCLUDES) $(VERIF_INCLUDES) $(NETLIST_INCLUDES) $(LOADER_INCLUDES) $(MODEL_INCLUDES) $(COMPILE_TRISC_INCLUDES) -Iverif/op_tests -Iverif 
VERIF_OP_TESTS_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp -lm


VERIF_OP_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_OP_TESTS_SRCS:.cpp=.o))
VERIF_OP_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_OP_TESTS_SRCS:.cpp=.d))

-include $(VERIF_OP_TESTS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
verif/op_tests: $(VERIF_OP_TESTS) 
verif/op_tests/all: $(VERIF_OP_TESTS)
verif/op_tests/%: $(TESTDIR)/verif/op_tests/%;

verif/op_tests/clean:
	@echo $(VERIF_OP_TESTS)
	rm -f test_gen*.cpp 

.PHONY: verif/op_tests/print
verif/op_tests/print:
	@echo $(VERIF_OP_TESTS_SRCS)

# FOR NOW: FIXME: We move the generated op tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/verif/op_tests/%
$(TESTDIR)/verif/op_tests/%: $(OBJDIR)/verif/op_tests/%.o $(BACKEND_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_OP_TESTS_INCLUDES) -o $@ $^ $(LDFLAGS) $(VERIF_OP_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/verif/op_tests/%.o
$(OBJDIR)/verif/op_tests/%.o: verif/op_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(VERIF_OP_TESTS_INCLUDES) -c -o $@ $< 

