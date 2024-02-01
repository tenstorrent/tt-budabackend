OUT ?= out
PREFIX ?= $(OUT)
WARNINGS ?= -Wdelete-non-virtual-dtor -Wreturn-type -Wswitch -Wuninitialized -Wno-unused-parameter
CONFIG_CFLAGS =
CONFIG_LDFLAGS =
CC ?= gcc
CXX ?= g++
CFLAGS ?= -MMD $(WARNINGS) -I. $(CONFIG_CFLAGS) -mavx2 -DBUILD_DIR=\"$(OUT)\" -DFMT_HEADER_ONLY -Ithird_party/fmt
CXXFLAGS ?= --std=c++17 -fvisibility-inlines-hidden
LDFLAGS ?= $(CONFIG_LDFLAGS) -Wl,-rpath,$(PREFIX)/lib -Lumd/build/lib -ldl -no-pie
SHARED_LIB_FLAGS = -shared -fPIC
STATIC_LIB_FLAGS = -fPIC
TARGET ?= $(OUT)/test
SRC_DIRS ?= .

SRCS := $(shell find $(SRC_DIRS) -name *.cpp -or -name *.c -or -name *.s)
OBJS := $(addsuffix .o,$(basename $(SRCS)))
DEPS := $(OBJS:.o=.d)

INC_DIRS := $(shell find $(SRC_DIRS) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

$(TARGET): clean $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) -o $@ $(LOADLIBES) $(LDLIBS)

clean:
	rm -fdr $(OUT)
	rm -fdr $(OBJS)
	mkdir $(OUT)

-include $(DEPS)
