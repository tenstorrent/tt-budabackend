# Every variable in subdir must be prefixed with subdir (emulating a namespace)
NET2HLKS_SRCS = $(wildcard src/net2hlks/src/*.cpp)
NET2HLKS = $(BINDIR)/net2hlks

NET2HLKS_OBJS = $(addprefix $(OBJDIR)/, $(NET2HLKS_SRCS:.cpp=.o))
NET2HLKS_DEPS = $(addprefix $(OBJDIR)/, $(NET2HLKS_SRCS:.cpp=.d))

NET2HLKS_OBJS += $(NETLIST_OBJS)
NET2HLKS_DEPS += $(NETLIST_DEPS)

NET2HLKS_INCLUDES = \
	-I$(BUDA_HOME)/src/net2hlks/inc \
	-I$(BUDA_HOME)/net2hlks_lib \
	-Iumd \


NET2HLKS_LDFLAGS = -ltt -ldevice -lstdc++fs -lcommon -lhwloc -lnet2hlks $(COREMODEL_SPARTA_LIBS)
NET2HLKS_INCLUDES += $(INCLUDE_DIRS)

-include $(NET2HLKS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
src/net2hlks: $(NET2HLKS)

ALL_NET2HLKS_DEPENDENCY_OBJS = $(NET2HLKS_OBJS) $(COMMON_LIB) $(NETLIST_LIB)

$(NET2HLKS): $(ALL_NET2HLKS_DEPENDENCY_OBJS) $(UMD_DEVICE_LIB) $(BACKEND_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $(ALL_NET2HLKS_DEPENDENCY_OBJS) $(LDFLAGS) $(NET2HLKS_LDFLAGS)

.PRECIOUS: $(OBJDIR)/src/net2hlks/%.o  
$(OBJDIR)/src/net2hlks/%.o: src/net2hlks/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NET2HLKS_INCLUDES) -c -o $@ $<
