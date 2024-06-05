ifeq ($(CONFIG), release)
RELEASE = 1
else
RELEASE = 0
endif

.PHONY: tb/llk_tb
tb/llk_tb: src/ckernels src/firmware tb/llk_tb/overlay
	$(MAKE) RELEASE=$(RELEASE) -C tb/llk_tb

.PHONY: clean_llk_tb
clean_llk_tb: clean
	$(MAKE) RELEASE=$(RELEASE) -C tb/llk_tb clean
	rm llk_out -r
