PYBIND11_DIR = $(BUDA_HOME)/third_party/pybind11
DEBUDA_PYBIND_SRCS  = $(wildcard dbd/pybind/src/*.cpp)
DEBUDA_PYBIND_LIB = $(LIBDIR)/tt_dbd_pybind.so

DEBUDA_PYBIND_LIB_OBJS = $(addprefix $(OBJDIR)/, $(DEBUDA_PYBIND_SRCS:.cpp=.o))
DEBUDA_PYBIND_LIB_DEPS = $(addprefix $(OBJDIR)/, $(DEBUDA_PYBIND_SRCS:.cpp=.d))

DEBUDA_PYBIND_LIB_INCLUDES = \
	$(BASE_INCLUDES) \
	-Idbd/server/lib/inc \
	-Idbd/pybind/inc \
	-I$(BUDA_HOME)/umd \
	-I$(PYBIND11_DIR)/include \
	-I/usr/include/$(PYTHON_VERSION) \

DEBUDA_PYBIND_LDFLAGS = -ldbdserver -ldevice -lyaml-cpp -Wl,-rpath,\$$ORIGIN/../lib:\$$ORIGIN -pthread

-include $(DEBUDA_PYBIND_LIB_DEPS)

.PRECIOUS: $(OBJDIR)/dbd/pybind/%.o
$(OBJDIR)/dbd/pybind/%.o: dbd/pybind/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(DEBUDA_PYBIND_LIB_INCLUDES) -c -o $@ $<

# Each module has a top level target as the entrypoint which must match the subdir name
dbd/pybind: $(DEBUDA_PYBIND_LIB)

$(DEBUDA_PYBIND_LIB): $(DEBUDA_PYBIND_LIB_OBJS) $(UMD_DEVICE_LIB) $(DEBUDA_SERVER_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(SHARED_LIB_FLAGS) -o $@ $^ $(LDFLAGS) $(DEBUDA_PYBIND_LDFLAGS)

include $(BUDA_HOME)/dbd/pybind/unit_tests/module.mk
