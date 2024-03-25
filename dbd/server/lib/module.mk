# List of configuration files that we want to embed
DEBUDA_SERVER_LIB_CONFIGURATION_FILES = \
	dbd/server/lib/configuration/blackhole.embed $(BUDA_HOME)/device/blackhole_8x10.yaml \
	dbd/server/lib/configuration/grayskull.embed $(BUDA_HOME)/device/grayskull_10x12.yaml \
	dbd/server/lib/configuration/wormhole.embed $(BUDA_HOME)/device/wormhole_8x10.yaml \
	dbd/server/lib/configuration/wormhole_b0.embed $(BUDA_HOME)/device/wormhole_b0_8x10.yaml \
	dbd/server/lib/src/../configuration/blackhole.embed $(BUDA_HOME)/device/blackhole_8x10.yaml \
	dbd/server/lib/src/../configuration/grayskull.embed $(BUDA_HOME)/device/grayskull_10x12.yaml \
	dbd/server/lib/src/../configuration/wormhole.embed $(BUDA_HOME)/device/wormhole_8x10.yaml \
	dbd/server/lib/src/../configuration/wormhole_b0.embed $(BUDA_HOME)/device/wormhole_b0_8x10.yaml

DEBUDA_SERVER_LIB_CONFIGURATION_YAML_FILES = $(filter %.yaml, $(DEBUDA_SERVER_LIB_CONFIGURATION_FILES))
DEBUDA_SERVER_LIB_CONFIGURATION_EMBED_FILES = $(filter %.embed, $(DEBUDA_SERVER_LIB_CONFIGURATION_FILES))

$(DEBUDA_SERVER_LIB_CONFIGURATION_EMBED_FILES): $(DEBUDA_SERVER_LIB_CONFIGURATION_YAML_FILES)
	@mkdir -p $(BUDA_HOME)/dbd/server/lib/configuration
	cat $(subst $@_,,$(filter $@_%, $(join $(addsuffix _,$(DEBUDA_SERVER_LIB_CONFIGURATION_EMBED_FILES)),$(DEBUDA_SERVER_LIB_CONFIGURATION_YAML_FILES)))) | xxd -i > $(BUDA_HOME)/$@

CREATE_ETHERNET_MAP_WORMHOLE_DBD = $(BINDIR)/debuda-create-ethernet-map-wormhole

# Every variable in subdir must be prefixed with subdir (emulating a namespace)
DEBUDA_SERVER_LIB_SRCS  = $(wildcard dbd/server/lib/src/*.cpp)
DEBUDA_SERVER_LIB = $(LIBDIR)/libdbdserver.a

DEBUDA_SERVER_LIB_OBJS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_LIB_SRCS:.cpp=.o))
DEBUDA_SERVER_LIB_DEPS = $(addprefix $(OBJDIR)/, $(DEBUDA_SERVER_LIB_SRCS:.cpp=.d))

DEBUDA_SERVER_LIB_INCLUDES = \
	$(BASE_INCLUDES) \
	-Idbd/server/lib/inc \
	-I$(BUDA_HOME)/umd \

-include $(DEBUDA_SERVER_LIB_DEPS)

.PRECIOUS: $(OBJDIR)/dbd/server/lib/%.o
$(OBJDIR)/dbd/server/lib/%.o: dbd/server/lib/%.cpp $(DEBUDA_SERVER_LIB_CONFIGURATION_EMBED_FILES)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEBUDA_SERVER_LIB_INCLUDES) -c -o $@ $<

# Each module has a top level target as the entrypoint which must match the subdir name
dbd/server/lib: $(DEBUDA_SERVER_LIB) $(CREATE_ETHERNET_MAP_WORMHOLE_DBD)

$(DEBUDA_SERVER_LIB): $(DEBUDA_SERVER_LIB_OBJS)
	@mkdir -p $(@D)
	ar rcs -o $@ $(DEBUDA_SERVER_LIB_OBJS)

ifeq ("$(HOST_ARCH)", "aarch64")
$(CREATE_ETHERNET_MAP_WORMHOLE_DBD): $(BUDA_HOME)/umd/device/bin/silicon/aarch64/create-ethernet-map
else
$(CREATE_ETHERNET_MAP_WORMHOLE_DBD): $(BUDA_HOME)/umd/device/bin/silicon/x86/create-ethernet-map
endif
	@mkdir -p $(@D)
	ln -s $^ $@
	chmod +x $@
