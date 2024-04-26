# Every variable in subdir must be prefixed with subdir (emulating a namespace)

PERF_LIB = $(LIBDIR)/libperf_lib.a
PERF_LIB_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
PERF_LIB_INCLUDES = $(COMMON_INCLUDES) $(MODEL_INCLUDES) -Iperf_lib -Iumd
PERF_LIB_LDFLAGS = -lcommon -lhwloc -lmodel
PERF_LIB_CFLAGS = $(CFLAGS) -Wno-terminate

# PERF_LIB_SRCS = $(wildcard perf_lib/*.cpp)
PERF_LIB_SRCS = \
	perf_lib/postprocess.cpp \
	perf_lib/create_reports.cpp \
	perf_lib/perf_utils.cpp \
	perf_lib/perf_descriptor.cpp \
	perf_lib/perf_state.cpp \
	perf_lib/perf_base.cpp \
	perf_lib/analyse.cpp \
	perf_lib/memory_profiler.cpp

PERF_LIB_OBJS = $(addprefix $(OBJDIR)/, $(PERF_LIB_SRCS:.cpp=.o))
PERF_LIB_DEPS = $(addprefix $(OBJDIR)/, $(PERF_LIB_SRCS:.cpp=.d))

PERF_LIB_PERF_TARGETS = perf_lib/perf_targets
PERF_LIB_TARGETS_SCRIPT = perf_lib/get_all_perf_targets.py

.PHONY: $(PERF_LIB_PERF_TARGETS)

-include $(PERF_LIB_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
perf_lib: $(PERF_LIB)

$(PERF_LIB): $(PERF_LIB_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(PERF_LIB_OBJS)

$(OBJDIR)/perf_lib/%.o: perf_lib/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(PERF_LIB_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(PERF_LIB_INCLUDES) $(PERF_LIB_DEFINES) -c -o $@ $<

$(PERF_LIB_PERF_TARGETS): $(PERF_LIB_TARGETS_SCRIPT)
	$(PERF_LIB_TARGETS_SCRIPT) --output-dir $(TARGETSDIR)