DEVICE?=grayskull
SEED?=0
LOOPS?=0
C?=unary/unary_op_constraints.sv
TLIST?=nightly
ARGS=
HARVESTED_ROWS?=0

OUT_DIR?=$(patsubst %/,%,$(dir $(C)))

OUT_FILENAME?=$(strip $(subst _constraints.sv,, $(notdir $(C))))_test_configs.$(TLIST)

$(info    OUT_DIR is $(OUT_DIR))
$(info    OUT_FILENAME is $(OUT_FILENAME))

TARGET_FILE?=configs/$(DEVICE)/$(OUT_DIR)/$(OUT_FILENAME)

make: clean
	mkdir build
	vcs -kdb -debug_access+all -full64 -R -sverilog +incdir+src/test_infra/includes/$(DEVICE) +incdir+src/test_infra src/test_infra/$(C) +ntb_random_seed=$(SEED) -o build/out +out=$(TARGET_FILE).yaml +num_rand_loops=$(LOOPS) $(ARGS) +define+HARVESTED_ROWS=$(HARVESTED_ROWS) +cmd=$(TARGET_FILE).sh
	rm -fdr csrc
	rm ucli.key
	rm -fdr build
clean:
	rm -fdr csrc
	rm -f ucli.key
	rm -fdr build
	rm -f $(TARGET_FILE).yaml $(TARGET_FILE).sh
