# Every variable in subdir must be prefixed with subdir (emulating a namespace)

LOADER_TESTS_CFLAGS = $(CFLAGS) -Werror

LOADER_TESTS += $(basename $(wildcard loader/tests/*.cpp))
LOADER_TESTS_SRCS = $(addsuffix .cpp, $(LOADER_TESTS))

LOADER_TEST_INCLUDES = $(LOADER_INCLUDES) $(RUNTIME_INCLUDES) -Iloader/tests -Icompile_trisc -Iverif
LOADER_TESTS_LDFLAGS = -ltt -ldevice -lstdc++fs -pthread -lloader -lruntime -lop_model -lyaml-cpp -lhwloc -lcommon -lhwloc -lstdc++fs

LOADER_TESTS_OBJS = $(addprefix $(OBJDIR)/, $(LOADER_TESTS_SRCS:.cpp=.o))
LOADER_TESTS_DEPS = $(addprefix $(OBJDIR)/, $(LOADER_TESTS_SRCS:.cpp=.d))

-include $(LOADER_TESTS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
loader/tests: $(LOADER_TESTS)
loader/tests/all: $(LOADER_TESTS)
loader/tests/%: $(TESTDIR)/loader/tests/% ;

.PRECIOUS: $(TESTDIR)/loader/tests/%
$(TESTDIR)/loader/tests/%: $(OBJDIR)/loader/tests/%.o $(BACKEND_LIB) $(LOADER_LIB) $(VERIF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(LOADER_TESTS_CFLAGS) $(CXXFLAGS) $(LOADER_TEST_INCLUDES) -o $@ $^ $(LDFLAGS) $(LOADER_TESTS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/loader/tests/%.o
$(OBJDIR)/loader/tests/%.o: loader/tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(LOADER_TESTS_CFLAGS) $(CXXFLAGS) $(LOADER_TEST_INCLUDES) -c -o $@ $<
