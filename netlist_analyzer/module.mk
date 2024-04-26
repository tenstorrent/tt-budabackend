# Every variable in subdir must be prefixed with subdir (emulating a namespace)

NETLIST_ANALYZER_INCLUDES = $(BASE_INCLUDES)

NETLIST_ANALYZER_LIB = $(LIBDIR)/libnetlist_analyzer.a
NETLIST_ANALYZER_DEFINES = -DGIT_HASH=$(shell git rev-parse HEAD)
NETLIST_ANALYZER_INCLUDES += -Icommon -Inetlist_analyzer -Inetlist $(MODEL_INCLUDES) -Inetlist_analyzer/analyzer
NETLIST_ANALYZER_LDFLAGS = -lyaml-cpp -L$(LIBDIR) -ltt -Wl,-rpath=$(LIBDIR)
NETLIST_ANALYZER_CFLAGS = $(CFLAGS) -Werror -Wall -Wno-unknown-pragmas -Wno-reorder

NETLIST_ANALYZER_SRCS = $(wildcard netlist_analyzer/*.cpp)

NETLIST_ANALYZER_OBJS = $(addprefix $(OBJDIR)/, $(NETLIST_ANALYZER_SRCS:.cpp=.o))
NETLIST_ANALYZER_DEPS = $(addprefix $(OBJDIR)/, $(NETLIST_ANALYZER_SRCS:.cpp=.d))

-include $(NETLIST_ANALYZER_DEPS)

include netlist_analyzer/analyzer/module.mk

NETLIST_ANALYZER_DPNRA_BIN = $(BINDIR)/dpnra

# Each module has a top level target as the entrypoint which must match the subdir name
.PHONY: netlist_analyzer

netlist_analyzer: backend src/net2pipe netlist_analyzer/analyzer $(BACKEND_LIB) $(ANALYZER_LIB) $(NETLIST_ANALYZER_LIB) $(NETLIST_LIB) $(OP_MODEL_LIB) $(NETLIST_ANALYZER_DPNRA_BIN)

$(NETLIST_ANALYZER_LIB): $(NETLIST_ANALYZER_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(NETLIST_ANALYZER_OBJS)

$(OBJDIR)/netlist_analyzer/%.o: netlist_analyzer/%.cpp $(COMMON_LIB) $(NETLIST_LIB) $(ANALYZER_LIB)
	@mkdir -p $(@D)
	$(CXX) $(NETLIST_ANALYZER_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(NETLIST_ANALYZER_INCLUDES) $(NETLIST_ANALYZER_DEFINES) -c -o $@ $(NETLIST_ANALYZER_LDFLAGS) $<

$(NETLIST_ANALYZER_DPNRA_BIN): backend $(NETLIST_ANALYZER_LIB) $(ANALYZER_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NETLIST_ANALYZER_TEST_INCLUDES) -o $@ $(NETLIST_ANALYZER_LIB) $(ANALYZER_LIB) $(NETLIST_ANALYZER_LDFLAGS)
