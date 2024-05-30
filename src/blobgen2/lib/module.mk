BLOBGEN2_BASE_DIR = 

# Every variable in subdir must be prefixed with subdir (emulating a namespace)
BLOBGEN2_SRCS =  $(wildcard src/blobgen2/lib/src/*.cpp)
BLOBGEN2_SRCS += $(wildcard src/blobgen2/lib/src/helpers/*.cpp)
BLOBGEN2_SRCS += $(wildcard src/blobgen2/lib/src/io/*.cpp)
BLOBGEN2_SRCS += $(wildcard src/blobgen2/lib/src/overlay_blob/*.cpp)
BLOBGEN2_SRCS += $(wildcard src/blobgen2/lib/src/overlay_generation/*.cpp)
# We add only phase_blob_filler_arch_specific.cpp for current arch.
# These files need noc_overlay_parameters.h which is arch specific, but other parts of
# the code in headers also need this file. So it is impossible to to always build all
# arch specific phase blob fillers, since that might require including two different noc_overlay_parameters.h
# files in the same translation unit.
BLOBGEN2_SRCS += $(wildcard src/blobgen2/lib/src/overlay_generation/$(ARCH_NAME)/*.cpp)
BLOBGEN2_LIB = $(LIBDIR)/libblobgen2.a

BLOBGEN2_OBJS = $(addprefix $(OBJDIR)/, $(BLOBGEN2_SRCS:.cpp=.o))
BLOBGEN2_DEPS = $(addprefix $(OBJDIR)/, $(BLOBGEN2_SRCS:.cpp=.d))


BLOBGEN2_INCLUDES = $(BASE_INCLUDES)

# Not necessary since whole budabackend root folder is already added to includes in main Makefile, but we should still
# specify our includes here in order to be explicit from the module makefile about what exactly we include.
BLOBGEN2_INCLUDES = \
	-Icommon \
	-Isrc/blobgen2/lib/inc \
	-Iumd \
	-Iutils \
	-I$(YAML_PATH) \
	-Isrc/pipegen2/lib/inc \
	-Isrc/firmware/riscv/common

# Including appropriate firmware definitions (stream_io_map.h etc).
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/wormhole/noc
else
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)
  BLOBGEN2_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)/noc
endif

# Include arch specific pipegen2 includes.
BLOBGEN2_INCLUDES += -Isrc/pipegen2/lib/arch_inc/$(ARCH_NAME)

BLOBGEN2_LDFLAGS = -lyaml-cpp -lm

-include $(BLOBGEN2_DEPS)

.PRECIOUS: $(OBJDIR)/src/blobgen2/%.o
$(OBJDIR)/src/blobgen2/%.o: src/blobgen2/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(BLOBGEN2_INCLUDES) -c -o $@ $< $(LDFLAGS) $(BLOBGEN2_LDFLAGS)

# Each module has a top level target as the entrypoint which must match the subdir name
src/blobgen2/lib: $(BLOBGEN2_LIB)

$(BLOBGEN2_LIB): $(BLOBGEN2_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(BLOBGEN2_OBJS)
