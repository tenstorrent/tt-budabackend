
ERISC_USE_PRECOMPILED_BINARIES = ${TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH} 
ifdef TT_BACKEND_ERISC_PRECOMPILED_BINARIES_PATH
# Environment variable is set, use the else branch
ERISC_MAKE =
ERISC_MAKE_CLEAN =
else
	# Precomiled headers are not set 
	ifeq ($(ARCH_NAME),$(filter $(ARCH_NAME),wormhole wormhole_b0 blackhole))
	ERISC_MAKE = BUDA_HOME=$(BUDA_HOME) $(MAKE) -C src/firmware/riscv/targets/erisc
	ERISC_MAKE_CLEAN = BUDA_HOME=$(BUDA_HOME) $(MAKE) -C src/firmware/riscv/targets/erisc clean
	else
	ERISC_MAKE =
	ERISC_MAKE_CLEAN =
	endif
endif

CONFIG?=develop
TARGET_DIR?=$(BUDA_HOME)/build/src/firmware/riscv/targets/
CONFIG_FILE?=$(TARGET_DIR)/.config
.PHONY: change-config

change-config:
	$(shell if [ ! -d $(TARGET_DIR) ]; then mkdir -p $(TARGET_DIR); fi)
    ifneq ($(shell cat $(CONFIG_FILE) 2>/dev/null),$(CONFIG))
	@+$(MAKE) src/firmware/clean
    endif
	@echo "$(CONFIG)" > $(CONFIG_FILE)

.PHONY: src/firmware
src/firmware: change-config $(BUDA_HOME)/src/ckernels
	@BUDA_HOME=$(BUDA_HOME) $(MAKE) -C src/firmware/riscv/targets/brisc
	@BUDA_HOME=$(BUDA_HOME) $(MAKE) -C src/firmware/riscv/targets/ncrisc
	@+$(ERISC_MAKE)

src/firmware/clean:
	@BUDA_HOME=$(BUDA_HOME) $(MAKE) -C src/firmware/riscv/targets/brisc clean
	@BUDA_HOME=$(BUDA_HOME) $(MAKE) -C src/firmware/riscv/targets/ncrisc clean
	@+$(ERISC_MAKE_CLEAN)
