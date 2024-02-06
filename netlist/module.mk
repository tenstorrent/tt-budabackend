# Every variable in subdir must be prefixed with subdir (emulating a namespace)

NETLIST_INCLUDES = $(BASE_INCLUDES)

NETLIST_LIB = $(LIBDIR)/libnetlist.a
NETLIST_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
NETLIST_INCLUDES += -Imodel $(COREMODEL_INCLUDES) -Inetlist -Iops -Iperf_lib -Icommon -I$(YAML_PATH) -Iumd
NETLIST_LDFLAGS = -lyaml-cpp -lcommon -lhwloc -lops -lop_model
NETLIST_CFLAGS = $(CFLAGS) -Werror -Wno-int-to-pointer-cast -Wunused-variable

NETLIST_SRCS = $(wildcard netlist/*.cpp)

NETLIST_OBJS = $(addprefix $(OBJDIR)/, $(NETLIST_SRCS:.cpp=.o))
NETLIST_DEPS = $(addprefix $(OBJDIR)/, $(NETLIST_SRCS:.cpp=.d))

-include $(NETLIST_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
netlist: $(NETLIST_LIB)

$(NETLIST_LIB): $(COMMON_LIB) $(OPS_LIB) $(NETLIST_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(NETLIST_OBJS)

$(OBJDIR)/netlist/%.o: netlist/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(NETLIST_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(NETLIST_INCLUDES) $(NETLIST_DEFINES) -c -o $@ $(NETLIST_LDFLAGS) $<

include netlist/tests/module.mk
# Include unit test modules
include $(BUDA_HOME)/netlist/unit_tests/module.mk
