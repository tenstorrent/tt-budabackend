# Every variable in subdir must be prefixed with subdir (emulating a namespace)

ANALYZER_INCLUDES = 

ANALYZER_LIB = $(LIBDIR)/libanalyzer.a
ANALYZER_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
ANALYZER_INCLUDES += -Inetlist_analyzer/analyzer -Inetlist_analyzer/analyzer/common
ANALYZER_LDFLAGS = -lyaml-cpp
ANALYZER_CFLAGS = $(CFLAGS) -Werror -Wall -Wno-unknown-pragmas -Wno-reorder

ANALYZER_SRCS += \
	$(wildcard netlist_analyzer/analyzer/*.cpp) \
	$(wildcard netlist_analyzer/analyzer/common/*.cpp)


ANALYZER_OBJS = $(addprefix $(OBJDIR)/, $(ANALYZER_SRCS:.cpp=.o))

ANALYZER_GTEST_SRCS = $(wildcard netlist_analyzer/analyzer/gtest*.cpp)
ANALYZER_GTEST_OBJS = $(addprefix $(OBJDIR)/, $(ANALYZER_GTEST_SRCS:.cpp=.o))

ANALYZER_DEPS = $(addprefix $(OBJDIR)/, $(ANALYZER_SRCS:.cpp=.d)) $(addprefix $(OBJDIR)/, $(ANALYZER_GTEST_SRCS:.cpp=.d))

-include $(ANALYZER_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
netlist_analyzer/analyzer: $(ANALYZER_LIB)

netlist_analyzer/gtest: $(ANALYZER_LIB)
	$(CXX) $(ANALYZER_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(ANALYZER_INCLUDES) $(ANALYZER_DEFINES) -L/usr/lib/ -o $@ $(ANALYZER_GTEST_OBJS) $< $(ANALYZER_LDFLAGS) -lgtest_main  -lgtest -lpthread 

$(ANALYZER_LIB): $(ANALYZER_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(ANALYZER_OBJS)

$(OBJDIR)/netlist_analyzer/analyzer/%.o: netlist_analyzer/analyzer/%.cpp 
	@mkdir -p $(@D)
	$(CXX) $(ANALYZER_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(ANALYZER_INCLUDES) $(ANALYZER_DEFINES) -c -o $@ $<

