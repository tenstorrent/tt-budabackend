PIPEGEN2_APP = $(BINDIR)/pipegen2

PIPEGEN2_APP_SRCS += $(wildcard src/pipegen2/app/*.cpp)

PIPEGEN2_APP_OBJS = $(addprefix $(OBJDIR)/, $(PIPEGEN2_APP_SRCS:.cpp=.o))
PIPEGEN2_APP_DEPS = $(addprefix $(OBJDIR)/, $(PIPEGEN2_APP_SRCS:.cpp=.d))

PIPEGEN2_APP_INCLUDES = \
 -Isrc/pipegen2/lib/inc \
 -Iumd \

# libcommon.a depends on libdevice.so which needs to be linked in with yaml-cpp and math.
PIPEGEN2_APP_LDFLAGS = -ldevice -lyaml-cpp -lm -Wl,-rpath,\$$ORIGIN/../lib

-include $(PIPEGEN2_APP_DEPS)

.PRECIOUS: $(OBJDIR)/src/pipegen2/%.o
$(OBJDIR)/src/pipegen2/app/%.o: src/pipegen2/app/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(PIPEGEN2_APP_INCLUDES) -c -o $@ $<

src/pipegen2/app: $(PIPEGEN2_APP)

$(PIPEGEN2_APP): $(PIPEGEN2_APP_OBJS) $(PIPEGEN2_LIB) $(UMD_DEVICE_LIB) $(COMMON_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -o $@ $(PIPEGEN2_APP_OBJS) $(PIPEGEN2_LIB) $(COMMON_LIB) $(LDFLAGS) $(PIPEGEN2_APP_LDFLAGS)