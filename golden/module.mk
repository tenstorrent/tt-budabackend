# Every variable in subdir must be prefixed with subdir (emulating a namespace)

GOLDEN_INCLUDES = $(BASE_INCLUDES)

GOLDEN_LIB = $(LIBDIR)/libgolden.a
GOLDEN_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
GOLDEN_INCLUDES += -Icommon -Igolden -Iops -Inetlist $(MODEL_INCLUDES) -Iumd
GOLDEN_LDFLAGS = -lyaml-cpp
GOLDEN_CFLAGS = $(CFLAGS) -Werror -Wall -Wno-unknown-pragmas -Wno-reorder

GOLDEN_SRCS = $(wildcard golden/*.cpp)

GOLDEN_OBJS = $(addprefix $(OBJDIR)/, $(GOLDEN_SRCS:.cpp=.o))
GOLDEN_DEPS = $(addprefix $(OBJDIR)/, $(GOLDEN_SRCS:.cpp=.d))

-include $(GOLDEN_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
golden: $(GOLDEN_LIB)

$(GOLDEN_LIB): $(GOLDEN_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(GOLDEN_OBJS)

$(OBJDIR)/golden/%.o: golden/%.cpp $(COMMON_LIB)
	@mkdir -p $(@D)
	$(CXX) $(GOLDEN_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(GOLDEN_INCLUDES) $(GOLDEN_DEFINES) -c -o $@ $(GOLDEN_LDFLAGS) $<

