.PHONY: make gen_test_configs gen_netlists clean cleanall

DEVICE          ?= grayskull
SEED            ?= 1
LOOPS           ?= 7
GRAPH           ?= softmax
C               ?= $(GRAPH)/$(GRAPH)_constraints.sv
TLIST           ?= nightly


ROOT_DIR         = $(shell readlink -m $(shell pwd)/../../../..)
GRAPH_TESTS_DIR  = $(ROOT_DIR)/verif/graph_tests

TST_CFG_INT_DIR  = $(patsubst %/,%,$(dir $(C)))
TST_CFG_OUT_DIR  = $(patsubst %/,%,$(dir $(C)))/out
TST_CFG_FILENAME = $(strip $(subst _constraints.sv,, $(notdir $(C))))_test_configs.$(TLIST).yaml

NETLIST_OUT_DIR  = $(GRAPH_TESTS_DIR)/netlists/$(GRAPH)-out

HARVESTED_ROWS?=0

#$(info ROOT_DIR=$(ROOT_DIR))
#$(info TST_CFG_FILENAME=$(GRAPH_TESTS_DIR)/constraints/vcs/$(TST_CFG_OUT_DIR)/$(DEVICE)/$(TST_CFG_FILENAME))
#$(info NETLIST_OUT_DIR=$(NETLIST_OUT_DIR))


make: clean \
	gen_test_configs \
	gen_netlists

gen_test_configs:
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	$(info Generating test configs for GRAPH=$(GRAPH), DEVICE=$(DEVICE), SEED=$(SEED), LOOPS=$(LOOPS), C=$(C), TLIST=$(TLIST)...)
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	mkdir out
	mkdir -p $(TST_CFG_OUT_DIR)/$(DEVICE)
	vcs -kdb -debug_access+all -full64 -R -sverilog +incdir+global/$(DEVICE) $(C) +ntb_random_seed=$(SEED) -o out/$(TST_CFG_INT_DIR) +out=$(TST_CFG_OUT_DIR)/$(DEVICE)/$(TST_CFG_FILENAME) +num_rand_loops=$(LOOPS) +define+HARVESTED_ROWS=$(HARVESTED_ROWS)
	rm -fdr csrc
	rm ucli.key
	rm -fdr out

gen_netlists:
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	$(info Generating netlists for GRAPH=$(GRAPH), DEVICE=$(DEVICE), SEED=$(SEED), LOOPS=$(LOOPS), C=$(C), TLIST=$(TLIST)...)
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	$(ROOT_DIR)/verif/template_netlist/generate_netlists.py \
		--template-yaml $(GRAPH_TESTS_DIR)/netlists/templates/$(GRAPH)/netlist_$(GRAPH).template.yaml \
		--test-configs-yaml $(GRAPH_TESTS_DIR)/constraints/vcs/$(GRAPH)/out/$(DEVICE)/$(GRAPH)_test_configs.$(TLIST).yaml \
		--output-dir $(NETLIST_OUT_DIR)

clean:
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	$(info Cleaning tenerated test configs for GRAPH=$(GRAPH), DEVICE=$(DEVICE), SEED=$(SEED), LOOPS=$(LOOPS), C=$(C), TLIST=$(TLIST)...)
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	rm -fdr out
	rm -fdr $(TST_CFG_OUT_DIR)/$(DEVICE)

cleanall:
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
	$(info Cleaning all ...)
	$(info ----------------------------------------------------------------------------------------------------------------------------------------------------)
#rm -fdr out
	rm -fdr $(TST_CFG_OUT_DIR)
	rm -fdr $(NETLIST_OUT_DIR)
