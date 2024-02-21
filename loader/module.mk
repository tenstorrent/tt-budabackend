# Every variable in subdir must be prefixed with subdir (emulating a namespace)

LOADER_LIB = $(LIBDIR)/libloader.a
LOADER_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
LOADER_INCLUDES = $(COMMON_INCLUDES) $(MODEL_INCLUDES) $(NETLIST_INCLUDES) -I$(BUDA_HOME)/loader -I$(BUDA_HOME)/runtime -I$(BUDA_HOME)/.

LOADER_INCLUDES += -I$(BUDA_HOME)/umd
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  LOADER_INCLUDES += -I$(BUDA_HOME)/umd/device/wormhole/
else
  LOADER_INCLUDES += -I$(BUDA_HOME)/umd/device/$(ARCH_NAME)/
endif
LOADER_LDFLAGS = -L$(BUDA_HOME) -lcommon -lhwloc -lnetlist
LOADER_CFLAGS = $(CFLAGS) -Werror -Wno-int-to-pointer-cast -Wno-abi

LOADER_SRCS = \
	loader/tlb_config.cpp \
	loader/tt_cluster.cpp \
	loader/epoch_loader.cpp \
	loader/epoch_utils.cpp \
	loader/utils.cpp \
	loader/tt_memory.cpp \
	loader/tt_hexfile.cpp \

LOADER_OBJS = $(addprefix $(OBJDIR)/, $(LOADER_SRCS:.cpp=.o))
LOADER_DEPS = $(addprefix $(OBJDIR)/, $(LOADER_SRCS:.cpp=.d))

-include $(LOADER_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
loader: $(LOADER_LIB)

$(LOADER_LIB): $(COMMON_LIB) $(NETLIST_LIB) $(LOADER_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(LOADER_OBJS)

$(OBJDIR)/loader/%.o: loader/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(LOADER_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(LOADER_INCLUDES) $(LOADER_DEFINES) -c -o $@ $< $(LOADER_LDFLAGS)

