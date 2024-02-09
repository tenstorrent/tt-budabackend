# Every variable in subdir must be prefixed with subdir (emulating a namespace)
NETLIST_UNIT_TESTS_SRCS += $(wildcard netlist/unit_tests/*.cpp)
NETLIST_UNIT_TESTS_BUILD_DIR = $(UTSDIR)/netlist
NETLIST_UNIT_TESTS_BIN_DIR = $(NETLIST_UNIT_TESTS_BUILD_DIR)/bin
NETLIST_UNIT_TESTS_OBJ_DIR = $(NETLIST_UNIT_TESTS_BUILD_DIR)/obj
NETLIST_UNIT_TESTS_BIN = $(NETLIST_UNIT_TESTS_BIN_DIR)/netlist_unit_tests
NETLIST_UNIT_TESTS_OBJS += $(addprefix $(NETLIST_UNIT_TESTS_OBJ_DIR)/, $(NETLIST_UNIT_TESTS_SRCS:.cpp=.o))
NETLIST_UNIT_TESTS_LDFLAGS = -ltt -lstdc++fs -lgtest -lpthread -lyaml-cpp

.PRECIOUS: $(NETLIST_UNIT_TESTS_BIN)
$(NETLIST_UNIT_TESTS_BIN): $(LIBDIR)/libtt.so $(NETLIST_UNIT_TESTS_OBJS)
	@echo "Building: $(NETLIST_UNIT_TESTS_BIN)"
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(NETLIST_UNIT_TESTS_LDFLAGS)

.PRECIOUS: $(NETLIST_UNIT_TESTS_OBJ_DIR)/netlist/unit_tests/%.o
$(NETLIST_UNIT_TESTS_OBJ_DIR)/netlist/unit_tests/%.o: netlist/unit_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(NETLIST_INCLUDES) -c -o $@ $<

.PHONY: netlist_unit_tests_run_only netlist/unit_tests

netlist_unit_tests_run_only:
	@echo "Running: $(NETLIST_UNIT_TESTS_BIN)"
	@LOGGER_LEVEL=DISABLED $(NETLIST_UNIT_TESTS_BIN)

netlist/unit_tests: $(NETLIST_UNIT_TESTS_BIN)
ifndef SKIP_UNIT_TESTS_RUN
	@$(MAKE) netlist_unit_tests_run_only
endif