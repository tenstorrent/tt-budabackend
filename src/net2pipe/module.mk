# Every variable in subdir must be prefixed with subdir (emulating a namespace)
NET2PIPE_SRCS = $(wildcard src/net2pipe/src/*.cpp)
NET2PIPE_SRCS += $(wildcard src/net2pipe/src/router/*.cpp)
NET2PIPE_SRCS += $(wildcard src/net2pipe/src/address_map_adaptors/l1/*.cpp)
NET2PIPE = $(BINDIR)/net2pipe

NET2PIPE_OBJS = $(addprefix $(OBJDIR)/, $(NET2PIPE_SRCS:.cpp=.o))
NET2PIPE_DEPS = $(addprefix $(OBJDIR)/, $(NET2PIPE_SRCS:.cpp=.d))

NET2PIPE_OBJS += $(NETLIST_OBJS)
NET2PIPE_DEPS += $(NETLIST_DEPS)

NET2PIPE_INCLUDES = \
	-I$(YAML_PATH) \
	-I$(BUDA_HOME)/src/net2pipe/inc \
	-I$(BUDA_HOME)/netlist \
	-I$(BUDA_HOME)/model \
	-Iumd

NET2PIPE_LDFLAGS = -ltt -ldevice -lop_model -lstdc++fs -lpthread -lyaml-cpp -lcommon -lhwloc -lgolden -lperf_lib $(COREMODEL_SPARTA_LIBS) -lzmq -Wl,-rpath,\$$ORIGIN/../lib -lm

ifeq ("$(ARCH_NAME)", "wormhole_b0")
  NET2PIPE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole
  NET2PIPE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/noc
  NET2PIPE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_b0_defines
else
  NET2PIPE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)
  NET2PIPE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/$(ARCH_NAME)/noc
endif

ifeq ("$(ARCH_NAME)", "wormhole")
  NET2PIPE_INCLUDES += -I$(BUDA_HOME)/src/firmware/riscv/wormhole/wormhole_a0_defines
endif

NET2PIPE_INCLUDES += $(INCLUDE_DIRS)

-include $(NET2PIPE_DEPS)

NET2PIPE_DEFINES = 
ifeq ("$(ARCH_NAME)", "wormhole")
NET2PIPE_DEFINES += -DRISC_A0_HW
endif

# Each module has a top level target as the entrypoint which must match the subdir name
src/net2pipe: $(NET2PIPE)

ALL_NET2PIPE_DEPENDENCY_OBJS = $(NET2PIPE_OBJS) $(OPS_LIB) $(MODEL_LIB) $(COMMON_LIB) $(NETLIST_LIB)

$(NET2PIPE): $(ALL_NET2PIPE_DEPENDENCY_OBJS) $(UMD_DEVICE_LIB) $(BACKEND_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $(ALL_NET2PIPE_DEPENDENCY_OBJS) $(LDFLAGS) $(NET2PIPE_LDFLAGS)

.PRECIOUS: $(OBJDIR)/src/net2pipe/%.o  
$(OBJDIR)/src/net2pipe/%.o: src/net2pipe/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NET2PIPE_INCLUDES) $(NET2PIPE_DEFINES) -c -o $@ $<


# Include unit test modules
include $(BUDA_HOME)/src/net2pipe/unit_tests/module.mk
