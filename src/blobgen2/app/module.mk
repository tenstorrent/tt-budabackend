BLOBGEN2_APP = $(BINDIR)/blobgen2

BLOBGEN2_APP_SRCS += $(wildcard src/blobgen2/app/*.cpp)

BLOBGEN2_APP_OBJS = $(addprefix $(OBJDIR)/, $(BLOBGEN2_APP_SRCS:.cpp=.o))
BLOBGEN2_APP_DEPS = $(addprefix $(OBJDIR)/, $(BLOBGEN2_APP_SRCS:.cpp=.d))

BLOBGEN2_APP_INCLUDES = \
 -Isrc/blobgen2/lib/inc \
 -Iumd \
 -Isrc/pipegen2/lib/inc \
 -Icommon \
 -Isrc/firmware/riscv/common

# Including appropriate firmware definitions (stream_io_map.h etc).
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  BLOBGEN2_APP_INCLUDES += -Isrc/firmware/riscv/wormhole
  BLOBGEN2_APP_INCLUDES += -Isrc/firmware/riscv/wormhole/noc
else
  BLOBGEN2_APP_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)
  BLOBGEN2_APP_INCLUDES += -Isrc/firmware/riscv/$(ARCH_NAME)/noc
endif

# libcommon.a depends on libdevice.so which needs to be linked in with yaml-cpp and math.
BLOBGEN2_APP_LDFLAGS = -lpipegen2 -ldevice -lcommon -lyaml-cpp -lm -Wl,-rpath,\$$ORIGIN/../lib

-include $(BLOBGEN2_APP_DEPS)

.PRECIOUS: $(OBJDIR)/src/blobgen2/%.o
$(OBJDIR)/src/blobgen2/app/%.o: src/blobgen2/app/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(BLOBGEN2_APP_INCLUDES) -c -o $@ $<

src/blobgen2/app: $(BLOBGEN2_APP)

$(BLOBGEN2_APP): $(BLOBGEN2_APP_OBJS) $(BLOBGEN2_LIB) $(UMD_DEVICE_LIB) $(COMMON_LIB) $(PIPEGEN2_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $(BLOBGEN2_APP_OBJS) $(BLOBGEN2_LIB) $(COMMON_LIB) $(LDFLAGS) $(BLOBGEN2_APP_LDFLAGS) 