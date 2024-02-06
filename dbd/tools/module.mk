#Every variable in subdir must be prefixed with subdir(emulating a namespace)
DBD_TOOLS += $(basename $(wildcard dbd/tools/*.cpp))

DBD_TOOLS_SRCS = $(addsuffix .cpp, $(DBD_TOOLS))

DBD_TOOLS_INCLUDES = $(GOLDEN_INCLUDES) $(DBD_INCLUDES) $(NETLIST_INCLUDES) $(LOADER_INCLUDES) $(MODEL_INCLUDES) $(COMPILE_TRISC_INCLUDES) -Idbd/tools -Iverif
DBD_TOOLS_LDFLAGS = -ltt -ldevice -lstdc++fs -lpthread -lyaml-cpp -lzmq

DBD_TOOLS_OBJS = $(addprefix $(OBJDIR)/, $(DBD_TOOLS_SRCS:.cpp=.o))
DBD_TOOLS_DEPS = $(addprefix $(OBJDIR)/, $(DBD_TOOLS_SRCS:.cpp=.d))

-include $(DBD_TOOLS_DEPS)
# Each module has a top level target as the entrypoint which must match the subdir name
dbd/tools: dbd/tools/run
dbd/tools/all: $(DBD_TOOLS)
dbd/tools/run: $(TESTDIR)/dbd/tools/run;

dbd/tools/clean:
	@echo $(DBD_TOOLS)
	@rm $(OBJDIR)/dbd/tools/* -v -f
	@rm $(TESTDIR)/dbd/tools/* -v -f

.PHONY: dbd/tools/print
dbd/tools/print:
	@echo $(DBD_TOOLS_SRCS)
	@echo $(DBD_TOOLS_OBJS)

# FOR NOW: FIXME: We move the generated graph tests back into model/test folder so the generated tests behave as if a model/test/*
.PRECIOUS: $(TESTDIR)/dbd/tools/%
$(TESTDIR)/dbd/tools/run: $(DBD_TOOLS_OBJS) $(BACKEND_LIB) $(VERIF_LIB)
	$(PRINT_TARGET)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DBD_TOOLS_INCLUDES) -o $@ $^ $(LDFLAGS) $(DBD_TOOLS_LDFLAGS)
	$(PRINT_OK)

.PRECIOUS: $(OBJDIR)/dbd/tools/%.o
$(OBJDIR)/dbd/tools/%.o: dbd/tools/%.cpp
	$(PRINT_TARGET)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DBD_TOOLS_INCLUDES) -c -o $@ $<
	$(PRINT_OK)
