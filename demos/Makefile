# Makefile for building test_standalone_runtime

# Compiler
CXX := g++

# Directories
BBE_LIB=../budabackend_lib
BUILD_DIR := ./
INCLUDE_DIR := $(BBE_LIB)/build/include
LIB_DIR := $(BBE_LIB)/build/lib

# Source file
SRC := test_standalone_runtime.cpp

# Object file
OBJ := $(BUILD_DIR)/obj/test_standalone_runtime.o

# Executable
TARGET := $(BUILD_DIR)/app/test_standalone_runtime

# Compiler flags
CXXFLAGS := -O3 -std=c++17 -I$(INCLUDE_DIR) -I$(INCLUDE_DIR)/backend

# Linker flags
LDFLAGS := -Wl,-rpath,$(LIB_DIR) -L$(LIB_DIR) -ldl -ldevice -ltt -lstdc++fs 

# Build rule for the executable
$(TARGET): $(OBJ)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Build rule for object file
$(BUILD_DIR)/obj/test_standalone_runtime.o: $(SRC)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Phony target to clean the build directory
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/app
	rm -rf $(BUILD_DIR)/obj

# Phony target to run the application
.PHONY: run
run: $(TARGET)
	./$(TARGET)