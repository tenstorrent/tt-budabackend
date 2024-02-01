OVERLAY_STICKY_FILE=$(OUT)/.overlay_ran

src/overlay: $(OVERLAY_STICKY_FILE)

$(OVERLAY_STICKY_FILE):
	@mkdir -p $(@D)
	@cd src/overlay && ruby blob_gen.rb > /dev/null
	@echo "TODO: src/overlay/module.mk copy blob_gen.rb to bin dir and have model.cpp reference that"
	@touch $(OVERLAY_STICKY_FILE)
