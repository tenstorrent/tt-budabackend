# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEBUDA_SERVER_UNIT_TESTS_SRCS  = $(wildcard dbd/server/unit_tests/*.cpp)

DEBUDA_SERVER_UNIT_TESTS_BUILD_DIR = $(UTSDIR)/dbd/server
DEBUDA_SERVER_UNIT_TESTS_BIN_DIR   = $(DEBUDA_SERVER_UNIT_TESTS_BUILD_DIR)/bin
DEBUDA_SERVER_UNIT_TESTS_OBJ_DIR   = $(DEBUDA_SERVER_UNIT_TESTS_BUILD_DIR)/obj
DEBUDA_SERVER_UNIT_TESTS_BIN       = $(DEBUDA_SERVER_UNIT_TESTS_BIN_DIR)/debuda_server_unit_tests

DEBUDA_SERVER_UNIT_TESTS_OBJS += $(addprefix $(DEBUDA_SERVER_UNIT_TESTS_OBJ_DIR)/, $(DEBUDA_SERVER_UNIT_TESTS_SRCS:.cpp=.o))

DEBUDA_SERVER_UNIT_TESTS_LDLIBS = -lgtest -lgmock -lgtest_main -lzmq -lpthread

DEBUDA_SERVER_UNIT_TESTS_INCLUDES = \
	-Idbd/server/lib/inc \
	-Idbd/server/unit_tests

$(DEBUDA_SERVER_UNIT_TESTS_BIN): $(DEBUDA_SERVER_UNIT_TESTS_OBJS) $(DEBUDA_SERVER_LIB)
	@echo "Building: $(DEBUDA_SERVER_UNIT_TESTS_BIN)"
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(DEBUDA_SERVER_UNIT_TESTS_LDLIBS)

.PRECIOUS: $(DEBUDA_SERVER_UNIT_TESTS_OBJ_DIR)/dbd/server/unit_tests/%.o
$(DEBUDA_SERVER_UNIT_TESTS_OBJ_DIR)/dbd/server/unit_tests/%.o: dbd/server/unit_tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DEBUDA_SERVER_LIB_INCLUDES) $(DEBUDA_SERVER_UNIT_TESTS_INCLUDES) -c -o $@ $<

.PHONY: dbd_server_unit_tests_run_only dbd/server/unit_tests

dbd_server_unit_tests_run_only:
	@echo "Running: $(DEBUDA_SERVER_UNIT_TESTS_BIN)"
	@$(DEBUDA_SERVER_UNIT_TESTS_BIN)

dbd/server/unit_tests: $(DEBUDA_SERVER_UNIT_TESTS_BIN)
ifndef SKIP_UNIT_TESTS_RUN
	@$(MAKE) dbd_server_unit_tests_run_only
endif