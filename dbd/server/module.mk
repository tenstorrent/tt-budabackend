# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEBUDA_SERVER_SRCS = $(wildcard dbd/server/*.cpp)
DEBUDA_SERVER = $(BINDIR)/debuda-server-standalone

# Libraries this target depends on
DEBUDA_SERVER_OBJS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_SRCS:.cpp=.o))
DEBUDA_SERVER_DEPS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_SRCS:.cpp=.d))

DEBUDA_SERVER_INCLUDES = $(RUNTIME_INCLUDES)

DEBUDA_SERVER_LDFLAGS = -ltt -ldevice -lyaml-cpp -lzmq -Wl,-rpath,\$$ORIGIN/../lib:\$$ORIGIN

-include $(DEBUDA_SERVER_DEPS)

dbd/server/debuda-server-standalone: $(DEBUDA_SERVER)

$(DEBUDA_SERVER): $(DEBUDA_SERVER_OBJS) $(BACKEND_LIB)
	echo "$(DEBUDA_SERVER_LDFLAGS)"
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) -ltt -o $@ $^ $(LDFLAGS) $(DEBUDA_SERVER_LDFLAGS)

.PRECIOUS: $(OBJDIR)/dbd/server/%.o
$(OBJDIR)/dbd/server/%.o: dbd/server/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DEBUDA_SERVER_INCLUDES) $(DEBUDA_SERVER_DEFINES) -c -o $@ $<
