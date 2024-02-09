# Every variable in subdir must be prefixed with subdir (emulating a namespace)
OP_MODEL_ESTIMATE_EVALUATION_INCLUDES = $(NETLIST_INCLUDES) $(OP_MODEL_INCLUDES) -Iperf_lib/op_model/estimate_evaluation
OP_MODEL_ESTIMATE_EVALUATION_LDFLAGS = -L$(BUDA_HOME) $(NETLIST_LDFLAGS) $(OP_MODEL_LDFLAGS) -lop_model -lnetlist
OP_MODEL_ESTIMATE_EVALUATION_CFLAGS = $(CFLAGS) -Werror -Wall

OP_MODEL_ESTIMATE_EVALUATION = $(BINDIR)/op_model_estimate_evaluation
OP_MODEL_ESTIMATE_EVALUATION_LIB = $(LIBDIR)/libop_model_estimate_evaluation.a
OP_MODEL_ESTIMATE_EVALUATION_SRCS += $(wildcard perf_lib/op_model/scripts/estimate_evaluation/*.cpp)

OP_MODEL_ESTIMATE_EVALUATION_OBJS = $(addprefix $(OBJDIR)/, $(OP_MODEL_ESTIMATE_EVALUATION_SRCS:.cpp=.o))

# Each module has a top level target as the entrypoint which must match the subdir name
perf_lib/op_model/scripts/estimate_evaluation: $(OP_MODEL_ESTIMATE_EVALUATION) $(OP_MODEL_ESTIMATE_EVALUATION_LIB)

$(OP_MODEL_ESTIMATE_EVALUATION): $(OP_MODEL_ESTIMATE_EVALUATION_OBJS) $(OP_MODEL_LIB) $(NETLIST_LIB)
	@mkdir -p $(@D)
	$(CXX) $(OP_MODEL_ESTIMATE_EVALUATION_CFLAGS) $(CXXFLAGS) $(OP_MODEL_ESTIMATE_EVALUATION_INCLUDES) -o $@ $^ $(LDFLAGS) $(OP_MODEL_ESTIMATE_EVALUATION_LDFLAGS)

$(OP_MODEL_ESTIMATE_EVALUATION_LIB): $(NETLIST_LIB) $(OP_MODEL_LIB) $(OP_MODEL_ESTIMATE_EVALUATION_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(OP_MODEL_ESTIMATE_EVALUATION_OBJS)

$(OBJDIR)/perf_lib/op_model/scripts/estimate_evaluation/%.o: perf_lib/op_model/scripts/estimate_evaluation/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(OP_MODEL_ESTIMATE_EVALUATION_CFLAGS) $(CXXFLAGS) $(OP_MODEL_ESTIMATE_EVALUATION_INCLUDES) -c -o $@ $< $(OP_MODEL_ESTIMATE_EVALUATION_LDFLAGS)
