# Every variable in subdir must be prefixed with subdir (emulating a namespace)

NET2HLKS_LIB_INCLUDES = $(BASE_INCLUDES)

NET2HLKS_LIB = $(LIBDIR)/libnet2hlks.a
NET2HLKS_LIB_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
NET2HLKS_LIB_INCLUDES += -I$(BUDA_HOME)/netlist -Iumd

NET2HLKS_LIB_SRCS = $(wildcard net2hlks_lib/*.cpp)

NET2HLKS_LIB_OBJS = $(addprefix $(OBJDIR)/, $(NET2HLKS_LIB_SRCS:.cpp=.o))
NET2HLKS_LIB_DEPS = $(addprefix $(OBJDIR)/, $(NET2HLKS_LIB_SRCS:.cpp=.d))

-include $(NET2HLKS_LIB_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
net2hlks_lib: $(NET2HLKS_LIB)

$(NET2HLKS_LIB): $(NET2HLKS_LIB_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(NET2HLKS_LIB_OBJS)

$(OBJDIR)/net2hlks_lib/%.o: net2hlks_lib/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(NET2HLKS_LIB_INCLUDES) $(NET2HLKS_LIB_DEFINES) -c -o $@ $<