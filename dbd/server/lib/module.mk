# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEBUDA_SERVER_LIB_SRCS  = $(wildcard dbd/server/lib/src/*.cpp)
DEBUDA_SERVER_LIB = $(LIBDIR)/libdbdserver.a

DEBUDA_SERVER_LIB_OBJS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_LIB_SRCS:.cpp=.o))
DEBUDA_SERVER_LIB_DEPS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_LIB_SRCS:.cpp=.d))

DEBUDA_SERVER_LIB_INCLUDES = \
	$(BASE_INCLUDES) \
	-Idbd/server/lib/inc \

DEBUDA_SERVER_LIB_INCLUDES = $(RUNTIME_INCLUDES) -I$(BUDA_HOME)/umd
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEBUDA_SERVER_LIB_INCLUDES += -I$(BUDA_HOME)/umd/device/wormhole/
else
  DEBUDA_SERVER_LIB_INCLUDES += -I$(BUDA_HOME)/umd/device/$(ARCH_NAME)/
endif

-include $(DEBUDA_SERVER_LIB_DEPS)

.PRECIOUS: $(OBJDIR)/dbd/server/lib/%.o
$(OBJDIR)/dbd/server/lib/%.o: dbd/server/lib/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEBUDA_SERVER_LIB_INCLUDES) -c -o $@ $<

# Each module has a top level target as the entrypoint which must match the subdir name
dbd/server/lib: $(DEBUDA_SERVER_LIB)

$(DEBUDA_SERVER_LIB): $(DEBUDA_SERVER_LIB_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(DEBUDA_SERVER_LIB_OBJS)
