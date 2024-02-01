# Every variable in subdir must be prefixed with subdir (emulating a namespace)

OPS_INCLUDES = $(BASE_INCLUDES)

OPS_LIB = $(LIBDIR)/libops.a
OPS_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)

OPS_INCLUDES += -Ihlks -Imodel -Icommon -Inetlist -I$(YAML_PATH) -Iumd

OPS_LDFLAGS = -lyaml-cpp -lcommon -lhwloc
#OPS_CFLAGS = $(CFLAGS) -Werror -Wno-int-to-pointer-cast
OPS_CFLAGS = $(CFLAGS) -Wno-int-to-pointer-cast -fpermissive 

OPS_SRCS = $(wildcard ops/*.cpp)

OPS_OBJS = $(addprefix $(OBJDIR)/, $(OPS_SRCS:.cpp=.o))
OPS_DEPS = $(addprefix $(OBJDIR)/, $(OPS_SRCS:.cpp=.d))

-include $(OPS_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
ops: $(OPS_LIB)

# Ops does include tt_ops and model structures, but really extends tt_op, so should not need to link everything. 
# users of ops should link model anyways
$(OPS_LIB): $(COMMON_LIB) $(OPS_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(OPS_OBJS)

$(OBJDIR)/ops/%.o: ops/%.cpp 
	@mkdir -p $(@D)
	$(CXX) $(OPS_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(OPS_INCLUDES) $(OPS_DEFINES) -c -o $@ $(OPS_LDFLAGS) $<
