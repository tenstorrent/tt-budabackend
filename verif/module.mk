#Every variable in subdir must be prefixed with subdir(emulating a namespace)

VERIF_INCLUDES = $(BASE_INCLUDES)
VERIF_LIB = $(LIBDIR)/libverif.a
VERIF_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
VERIF_INCLUDES += -Imodel -Inetlist -Icommon -Iumd -I$(YAML_PATH) -Isrc/firmware/riscv/$(ARCH_NAME)/

VERIF_CFLAGS = $(CFLAGS) -Werror 

VERIF_SRCS = $(wildcard verif/*.cpp)

VERIF_OBJS = $(addprefix $(OBJDIR)/, $(VERIF_SRCS:.cpp=.o))
VERIF_DEPS = $(addprefix $(OBJDIR)/, $(VERIF_SRCS:.cpp=.d))

-include $(VERIF_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
verif: $(VERIF_LIB)

$(VERIF_LIB): $(VERIF_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(VERIF_OBJS)

$(OBJDIR)/verif/%.o: verif/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(VERIF_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(VERIF_INCLUDES) $(VERIF_DEFINES) -c -o $@ $<

include verif/directed_tests/module.mk
include verif/netlist_tests/module.mk
include verif/op_tests/module.mk
include verif/tm_tests/module.mk
include verif/graph_tests/module.mk
include verif/error_tests/module.mk