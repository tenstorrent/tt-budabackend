# Every variable in subdir must be prefixed with subdir (emulating a namespace)

OP_MODEL_LIB = $(LIBDIR)/libop_model.a
OP_MODEL_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
OP_MODEL_INCLUDES = $(COMMON_INCLUDES) $(MODEL_INCLUDES) $(NETLIST_INCLUDES) -Iperf_lib/op_model
OP_MODEL_LDFLAGS = -L$(BUDA_HOME) -lmodel -lyaml-cpp

OP_MODEL_SRCS = $(wildcard perf_lib/op_model/*.cpp)
OP_MODEL = $(BINDIR)/op_model

OP_MODEL_OBJS = $(addprefix $(OBJDIR)/, $(OP_MODEL_SRCS:.cpp=.o))
OP_MODEL_DEPS = $(addprefix $(OBJDIR)/, $(OP_MODEL_SRCS:.cpp=.d))

-include $(OP_MODEL_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
perf_lib/op_model: $(OP_MODEL) $(OP_MODEL_LIB)

$(OP_MODEL): $(OP_MODEL_OBJS) $(MODEL_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(OP_MODEL_INCLUDES) $(OP_MODEL_DEFINES) -o $@ $^ $(LDFLAGS) $(OP_MODEL_LDFLAGS)

 $(OP_MODEL_LIB): $(COMMON_LIB) $(MODEL_LIB) $(NETLIST_LIB) $(OP_MODEL_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(OP_MODEL_OBJS)

$(OBJDIR)/perf_lib/op_model/%.o: perf_lib/op_model/%.cpp 
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(OP_MODEL_INCLUDES) $(OP_MODEL_DEFINES) -c -o $@ $< $(OP_MODEL_LDFLAGS)

include perf_lib/op_model/scripts/estimate_evaluation/module.mk
include perf_lib/op_model/unit_tests/module.mk
