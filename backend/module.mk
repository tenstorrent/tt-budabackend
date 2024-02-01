# Every variable in subdir must be prefixed with subdir (emulating a namespace)

BACKEND_LIB = $(LIBDIR)/libtt.so
BACKEND_HEADERS = \
	netlist/tt_backend.hpp \
	netlist/tt_backend_api_types.hpp

# Each module has a top level target as the entrypoint which must match the subdir name
backend-api: $(BACKEND_HEADERS)
	@mkdir -p $(INCDIR)
	cp $^ $(INCDIR)/

backend: backend-api $(BACKEND_LIB)

$(BACKEND_LIB): \
            $(COMMON_LIB) \
            $(OPS_LIB) \
            $(GOLDEN_LIB) \
            $(NETLIST_LIB) \
            $(NET2HLKS_LIB) \
            $(LOADER_LIB) \
            $(PIPEGEN2_LIB) \
            $(RUNTIME_LIB) \
            $(MODEL_LIB) \
            $(COMPILE_TRISC_LIB) \
            $(PERF_LIB) \
            $(COREMODEL_LIB) \
            $(OP_MODEL_LIB) \
            $(DEBUDA_SERVER_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(SHARED_LIB_FLAGS) -o $@ -Wl,--whole-archive $^ -Wl,--no-whole-archive $(LDFLAGS) -ldevice -lperf_lib -lop_model -lhwloc -lzmq $(COREMODEL_SPARTA_LIBS)
