# Every variable in subdir must be prefixed with subdir (emulating a namespace)

BACKEND_CONFIG ?= develop
BACKEND_ARCH_NAME ?= grayskull

BACKEND_INCLUDES = $(COMMON_INCLUDES) -I$(BUDA_HOME)/third_party/pybind11/include -I/usr/include/$(PYTHON_VERSION)

BUDABACKEND_LIBDIR = $(BUDA_HOME)/build/lib
BUDABACKEND_LIB = $(BUDABACKEND_LIBDIR)/libtt.so
BUDABACKEND_DEVICE = $(BUDABACKEND_LIBDIR)/libdevice.so
BUDABACKEND_LDFLAGS = -L$(BUDA_HOME) -ltt -ldevice -lcommon -lop_model -lhwloc -lstdc++fs

BACKEND_PY_API_LIB = $(LIBDIR)/libbackend_api.a
BACKEND_PY_API_SRCS += \
	py_api/eager_backend.cpp

BACKEND_PY_API_INCLUDES = $(BACKEND_INCLUDES) -I$(BUDABACKEND_LIBDIR)

BACKEND_PY_API_OBJS = $(addprefix $(OBJDIR)/, $(BACKEND_PY_API_SRCS:.cpp=.o))
BACKEND_PY_API_DEPS = $(addprefix $(OBJDIR)/, $(BACKEND_PY_API_SRCS:.cpp=.d))

-include $(BACKEND_PY_API_DEPS)

# Each module has a top level target as the entrypoint which must match the subdir name
py_api: $(BACKEND_PY_API_LIB) $(BUDABACKEND_LIB) $(BUDABACKEND_DEVICE) ;

$(BACKEND_PY_API_LIB): $(BACKEND_PY_API_OBJS) $(BUDABACKEND_LIB) $(BUDABACKEND_DEVICE)
	@mkdir -p $(LIBDIR)
	ar rcs $@ $^

$(OBJDIR)/py_api/%.o: py_api/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(BACKEND_PY_API_INCLUDES) -c -o $@ $< $(BUDABACKEND_LDFLAGS)

EXT_SUFFIX := $(shell python3-config --extension-suffix)

eager_backend: $(BACKEND_PY_API_LIB) $(BUDABACKEND_LIB) $(BUDABACKEND_DEVICE)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(BACKEND_PY_API_INCLUDES) -fPIC -shared -o $(OBJDIR)/py_api/$@$(EXT_SUFFIX) $^ $(BACKEND_PY_API_SRCS) $(LDFLAGS) $(BUDABACKEND_LDFLAGS)
	
install_torch: $(BACKEND_PY_API_LIB) $(BUDABACKEND_LIB) $(BUDABACKEND_DEVICE)
	pip3 install torch --target build/obj/py_api