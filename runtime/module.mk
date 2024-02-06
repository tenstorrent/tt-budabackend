# Every variable in subdir must be prefixed with subdir (emulating a namespace)

RUNTIME_LIB = $(LIBDIR)/libruntime.a
RUNTIME_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
RUNTIME_INCLUDES = $(COMMON_INCLUDES) $(LOADER_INCLUDES) -I$(BUDA_HOME)/runtime -I$(BUDA_HOME)/src/pipegen2/lib/inc

RUNTIME_LDFLAGS = -L$(BUDA_HOME) -lcommon -lhwloc -lnetlist -lloader
RUNTIME_CFLAGS = $(CFLAGS) -Werror -Wno-int-to-pointer-cast

RUNTIME_SRCS = \
	runtime/tt_log_server.cpp \
	runtime/runtime_io.cpp \
	runtime/runtime_eager_io.cpp \
	runtime/runtime_utils.cpp \
	runtime/runtime_workload.cpp \
	runtime/runtime.cpp \
	runtime/runtime_params.cpp \
	runtime/debuda_server.cpp

RUNTIME_OBJS = $(addprefix $(OBJDIR)/, $(RUNTIME_SRCS:.cpp=.o))
RUNTIME_DEPS = $(addprefix $(OBJDIR)/, $(RUNTIME_SRCS:.cpp=.d))

-include $(RUNTIME_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
runtime: $(RUNTIME_LIB)

$(RUNTIME_LIB): $(COMMON_LIB) $(NETLIST_LIB) $(PIPEGEN2_LIB) $(RUNTIME_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(RUNTIME_OBJS)

$(OBJDIR)/runtime/%.o: runtime/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(RUNTIME_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(RUNTIME_INCLUDES) $(RUNTIME_DEFINES) -c -o $@ $< $(RUNTIME_LDFLAGS)

