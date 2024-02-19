# List of configuration files that we want to embed
CONFIGURATION_FILES = \
	dbd/server/app/configuration/blackhole.embed $(BUDA_HOME)/device/blackhole_8x10.yaml \
	dbd/server/app/configuration/grayskull.embed $(BUDA_HOME)/device/grayskull_10x12.yaml \
	dbd/server/app/configuration/wormhole.embed $(BUDA_HOME)/device/wormhole_8x10.yaml \
	dbd/server/app/configuration/wormhole_b0.embed $(BUDA_HOME)/device/wormhole_b0_8x10.yaml

CONFIGURATION_YAML_FILES = $(filter %.yaml, $(CONFIGURATION_FILES))
CONFIGURATION_EMBED_FILES = $(filter %.embed, $(CONFIGURATION_FILES))

$(CONFIGURATION_EMBED_FILES): $(CONFIGURATION_YAML_FILES)
	@mkdir -p $(BUDA_HOME)/dbd/server/app/configuration
	cat $(subst $@_,,$(filter $@_%, $(join $(addsuffix _,$(CONFIGURATION_EMBED_FILES)),$(CONFIGURATION_YAML_FILES)))) | xxd -i > $(BUDA_HOME)/$@

# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEBUDA_SERVER_SRCS = $(wildcard dbd/server/app/*.cpp)
DEBUDA_SERVER = $(BINDIR)/debuda-server-standalone
CREATE_ETHERNET_MAP_WORMHOLE_DBD = $(BINDIR)/debuda-create-ethernet-map-wormhole

# Libraries this target depends on
DEBUDA_SERVER_LINK_DEPS = $(DEBUDA_SERVER_LIB) $(BACKEND_LIB)
# Also add those libraries as with shared name to avoid hardcoding library paths.
DEBUDA_SERVER_LIB_DEPS = -ldbdserver -ltt -ldevice

DEBUDA_SERVER_OBJS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_SRCS:.cpp=.o))
DEBUDA_SERVER_DEPS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_SRCS:.cpp=.d))

DEBUDA_SERVER_INCLUDES = $(RUNTIME_INCLUDES) -I$(BUDA_HOME)/umd
ifeq ("$(ARCH_NAME)", "wormhole_b0")
  DEBUDA_SERVER_INCLUDES += -I$(BUDA_HOME)/umd/device/wormhole/
else
  DEBUDA_SERVER_INCLUDES += -I$(BUDA_HOME)/umd/device/$(ARCH_NAME)/
endif

DEBUDA_SERVER_LDFLAGS = $(DEBUDA_SERVER_LIB_DEPS) -lyaml-cpp -lzmq -Wl,-rpath,\$$ORIGIN/../lib:\$$ORIGIN -pthread

-include $(DEBUDA_SERVER_DEPS)

dbd/server/app: $(DEBUDA_SERVER) $(CREATE_ETHERNET_MAP_WORMHOLE_DBD)

$(CREATE_ETHERNET_MAP_WORMHOLE_DBD): $(BUDA_HOME)/umd/device/bin/silicon/wormhole/create-ethernet-map
	@mkdir -p $(@D)
	ln -s $^ $@
	chmod +x $@


$(DEBUDA_SERVER): $(DEBUDA_SERVER_OBJS) $(DEBUDA_SERVER_LINK_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DEBUDA_SERVER_LIB_DEPS) -o $@ $^ $(LDFLAGS) $(DEBUDA_SERVER_LDFLAGS)

.PRECIOUS: $(OBJDIR)/dbd/server/app/%.o
$(OBJDIR)/dbd/server/app/%.o: dbd/server/app/%.cpp $(CONFIGURATION_EMBED_FILES)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DEBUDA_SERVER_INCLUDES) $(DEBUDA_SERVER_DEFINES) -c -o $@ $<
