# Every variable in subdir must be prefixed with subdir (emulating a namespace)
COMPILE_TRISC_INCLUDES = $(BASE_INCLUDES)

COMPILE_TRISC_LIB = $(LIBDIR)/libcompile_trisc.a
COMPILE_TRISC_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
COMPILE_TRISC_INCLUDES += -Inetlist -Icompile_trisc $(MODEL_INCLUDES)
COMPILE_TRISC_LDFLAGS = -lmodel -lyaml-cpp
COMPILE_TRISC_CFLAGS = $(CFLAGS) -Werror

COMPILE_TRISC_SRCS = \
	compile_trisc/compile_trisc.cpp 

COMPILE_TRISC_OBJS = $(addprefix $(OBJDIR)/, $(COMPILE_TRISC_SRCS:.cpp=.o))
COMPILE_TRISC_DEPS = $(addprefix $(OBJDIR)/, $(COMPILE_TRISC_SRCS:.cpp=.d))

-include $(COMPILE_TRISC_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
compile_trisc: $(COMPILE_TRISC_LIB)

$(COMPILE_TRISC_LIB): $(COMPILE_TRISC_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(COMPILE_TRISC_OBJS)

$(OBJDIR)/compile_trisc/%.o: compile_trisc/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(COMPILE_TRISC_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(COMPILE_TRISC_INCLUDES) $(COMPILE_TRISC_DEFINES) -c -o $@ $<

include compile_trisc/tests/module.mk
