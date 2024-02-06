# Every variable in subdir must be prefixed with subdir (emulating a namespace)

# The list of unit test files
LOADER_UNIT_TESTS_SRC_DIR = loader/unit_tests
LOADER_UNIT_TESTS_SRCS  = $(wildcard $(LOADER_UNIT_TESTS_SRC_DIR)/*.cpp)

# The build output paths
LOADER_UNIT_TESTS_BUILD_DIR = $(UTSDIR)/loader
LOADER_UNIT_TESTS_BIN_DIR   = $(LOADER_UNIT_TESTS_BUILD_DIR)/bin
LOADER_UNIT_TESTS_OBJ_DIR   = $(LOADER_UNIT_TESTS_BUILD_DIR)/obj
LOADER_UNIT_TESTS_BIN       = $(LOADER_UNIT_TESTS_BIN_DIR)/loader_unit_tests

# Corresponding object files and dependency files
LOADER_UNIT_TESTS_OBJS = $(addprefix $(LOADER_UNIT_TESTS_OBJ_DIR)/, $(LOADER_UNIT_TESTS_SRCS:.cpp=.o))

LOADER_UNIT_TESTS_LDFLAGS = -ltt -ldevice -lstdc++fs -pthread -lruntime -lop_model -lyaml-cpp -lcommon -lhwloc -lgtest -lgtest_main

# Include paths
LOADER_UNIT_TESTS_INCLUDES = $(LOADER_INCLUDES) -I$(LOADER_UNIT_TESTS_SRC_DIR) -Icompile_trisc -Iverif

# Libraries this target depends on
LOADER_UNIT_TESTS_LIB_DEPS = $(BACKEND_LIB) $(LOADER_LIB) $(VERIF_LIB)

LOADER_UNIT_TESTS_CFLAGS = $(CFLAGS) -Werror

$(LOADER_UNIT_TESTS_BIN): $(LOADER_UNIT_TESTS_OBJS) $(LOADER_UNIT_TESTS_LIB_DEPS)
	@echo "Building: $(LOADER_UNIT_TESTS_BIN) with UMD_VERSIM_STUB=$(UMD_VERSIM_STUB)"
	@mkdir -p $(@D)
	$(CXX) $(LOADER_UNIT_TESTS_CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LOADER_UNIT_TESTS_LDFLAGS)

# Rule to compile each object file
.PRECIOUS: $(LOADER_UNIT_TESTS_OBJ_DIR)/$(LOADER_UNIT_TESTS_SRC_DIR)/%.o
$(LOADER_UNIT_TESTS_OBJ_DIR)/$(LOADER_UNIT_TESTS_SRC_DIR)/%.o: $(LOADER_UNIT_TESTS_SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(LOADER_UNIT_TESTS_CFLAGS) $(CXXFLAGS) $(LOADER_UNIT_TESTS_INCLUDES) -c -o $@ $< 

.PHONY: loader_unit_tests_run_only $(LOADER_UNIT_TESTS_SRC_DIR)

loader_unit_tests_run_only:
	@echo "Running: $(LOADER_UNIT_TESTS_BIN)"
	@$(LOADER_UNIT_TESTS_BIN) --gtest_filter='EpochControl.*'
	@$(LOADER_UNIT_TESTS_BIN) --gtest_filter='BackendParamLib.*CoordTranslation'
	@$(LOADER_UNIT_TESTS_BIN) --gtest_filter='BackendParamLib.*:-BackendParamLib.*CoordTranslation'
	@$(LOADER_UNIT_TESTS_BIN) --gtest_filter='BackendPerf.*OpModelAPI*'
	@$(LOADER_UNIT_TESTS_BIN) --gtest_filter='BackendPerf.*:-BackendPerf.*OpModelAPI*'

# Rule to link the final test binary
$(LOADER_UNIT_TESTS_SRC_DIR): $(LOADER_UNIT_TESTS_BIN)
ifndef SKIP_UNIT_TESTS_RUN
	@$(MAKE) loader_unit_tests_run_only
endif