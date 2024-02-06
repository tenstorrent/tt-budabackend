#OUT ?= out
#PREFIX ?= $(OUT)
#WARNINGS ?= -Wdelete-non-virtual-dtor -Wreturn-type -Wswitch -Wuninitialized -Wno-unused-parameter
#CONFIG_CFLAGS =
#CONFIG_LDFLAGS =
#CC ?= gcc
#CXX ?= g++
#CFLAGS ?= -MMD $(WARNINGS) -I. $(CONFIG_CFLAGS) -mavx2 -DBUILD_DIR=\"$(OUT)\" -DFMT_HEADER_ONLY -Ithird_party/fmt
#CXXFLAGS ?= --std=c++17 -fvisibility-inlines-hidden
#LDFLAGS ?= $(CONFIG_LDFLAGS) -Wl,-rpath,$(PREFIX)/lib -Ldevice/lib -ldl -no-pie
#SHARED_LIB_FLAGS = -shared -fPIC
#STATIC_LIB_FLAGS = -fPIC
#TARGET ?= $(OUT)/spm
#SRC_DIRS ?= .

SPM_SRCS = $(wildcard verif/op_tests/tools/spm/*.cpp)
SPM = $(BINDIR)/spm
SPM_OBJS = $(addprefix $(OBJDIR)/, $(SPM_SRCS:.cpp=.o))
SPM_DEPS = $(addprefix $(OBJDIR)/, $(SPM_SRCS:.cpp=.d))
SPM_INCLUDES = \
	-Isrc/verif/tools/spm

-include $(SPM_DEPS)

verif/op_tests/tools/spm: $(SPM)

$(SPM): $(SPM_OBJS)
	@mkdir -p $(@D)
	@$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lm

.PRECIOUS: $(OBJDIR)/verif/op_tests/tools/spm/%.o
$(OBJDIR)/verif/op_tests/tools/spm/%.o: verif/op_tests/tools/spm/%.cpp
	@mkdir -p $(@D)
	@$(CXX) $(CFLAGS) $(CXXFLAGS) $(SPM_INCLUDES) -c -o $@ $<
