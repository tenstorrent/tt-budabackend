OVERLAY_STICKY_FILE=$(OUT)/.overlay_ran

tb/llk_tb/overlay: $(OVERLAY_STICKY_FILE)

$(OVERLAY_STICKY_FILE):
	@mkdir -p $(@D)
	@cd tb/llk_tb/overlay && ruby blob_gen.rb > /dev/null
	@echo "TODO: tb/llk_tb/overlay/module.mk copy blob_gen.rb to bin dir and have model.cpp reference that"
	@touch $(OVERLAY_STICKY_FILE)
