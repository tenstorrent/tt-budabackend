include $(BUDA_HOME)/infra/common.mk

DBD_OUT?=$(OUT)/dbd

# Main target: it builds the standalone server executable
dbd: dbd/server dbd/pybind
	$(PRINT_TARGET)
	$(PRINT_OK)

# Tests target: it builds everything and tests and run tests
dbdtests: dbd dbd/server/unit_tests dbd/pybind/unit_tests

# MARKDOWN_FILES=
MARKDOWN_FILES=debuda-py-intro.md
MARKDOWN_FILES+=debuda-py-tutorial.md
MARKDOWN_FILES+=$(DBD_OUT)/debuda-commands-help.md

# The following target is used to build the documentation (md, html, pdf). It is not bullet proof, but it is good enough for now.
.PHONY: dbd/documentation
dbd/documentation:
	echo "Output directory: $(DBD_OUT)"
	if [ "$(CONFIG)" = "debug" ]; then \
		echo "WARNING: Do not use CONFIG=debug. Please unset CONFIG"; \
		exit 1; \
	fi
	echo "Installing pandoc and weasyprint"
	sudo apt update && sudo apt-get -y install pandoc weasyprint
	pip install PyPDF2 reportlab
	echo Applying patch to cause a hang in the test
	-git apply dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-wormhole_b0.patch
	echo "Building and running test"
	$(MAKE) -j32 build_hw verif/op_tests dbd
	-./build/test/verif/op_tests/test_op --netlist dbd/test/netlists/netlist_multi_matmul_perf.yaml --seed 0 --silicon --timeout 60
	mkdir -p $(DBD_OUT)
	echo "Removing old export files"
	-rm -f *-export.[0-9]*.zip
	echo "Generating raw help file"
	dbd/debuda.py --command 'help -v;x' | tee $(DBD_OUT)/help_file_raw.txt
	echo "Generating help file with example outputs"
	dbd/bin/run-debuda-on-help-examples.py $(DBD_OUT)/help_file_raw.txt $(DBD_OUT)/help_file_with_example_outputs.txt
	echo "Generating Markdown $(DBD_OUT)/debuda-commands-help.md"
	# Check if grep -c ">>>>> ERROR" help_file_with_example_outputs.txt returns more than 0
	NUM_ERRORS=`grep -c ">>>>> ERROR" $(DBD_OUT)/help_file_with_example_outputs.txt` ; \
	if [ $$NUM_ERRORS -gt 0 ]; then \
		cat $(DBD_OUT)/help_file_with_example_outputs.txt | grep ">>>>> ERROR"; \
		echo "ERROR: There are $$NUM_ERRORS errors in the help file $(DBD_OUT)/help_file_with_example_outputs.txt. Please fix them before generating the documentation."; \
		exit 1; \
	fi
	dbd/bin/convert-help-to-markdown.py $(DBD_OUT)/help_file_with_example_outputs.txt $(DBD_OUT)/debuda-commands-help.md
	echo "Generating the CSS file to be included in the html"
	echo "<style>" > $(DBD_OUT)/temp-style-header.html
	cat dbd/docs-md/debuda-docs.css >> $(DBD_OUT)/temp-style-header.html
	echo "</style>" >> $(DBD_OUT)/temp-style-header.html
	echo "Combining markdown files into one html file"
	cd dbd/docs-md && pandoc -f markdown -s $(MARKDOWN_FILES) -o $(DBD_OUT)/debuda-help.html --metadata pagetitle="Debuda Documentation" --include-in-header=$(DBD_OUT)/temp-style-header.html
	cp -r dbd/docs-md/images $(DBD_OUT)
	echo "Generating pdf file"
	cd dbd/docs-md && pandoc -f markdown -s $(MARKDOWN_FILES) -o $(DBD_OUT)/debuda-help.pdf --pdf-engine=weasyprint --metadata pagetitle="Debuda Documentation" --include-in-header=$(DBD_OUT)/temp-style-header.html
	echo "Create a title page and insert it in side the pdf file"
	dbd/bin/create-title-page.py --title="Debuda Manual" --subtitle=" `date +%Y/%m/%d`" --image=dbd/docs-md/images/tenstorrent-pdf-titlepage.png --footer "" --pdf $(DBD_OUT)/debuda-help.pdf

	echo "Undoing the patch"
	-git apply -R dbd/test/inject-errors/sfpu_reciprocal-infinite-spin-wormhole_b0.patch

# The following target is used to build the release package (zip). It depends on dbd/documentation (as it needs the pdf file),
# however this is not encoded in the dependency list as it takes a while to build the documentation.
.PHONY: dbd/release
dbd/release: dbd dbd/documentation
	echo "The board should be reset before running this target."
	echo "Full command: unset CONFIG ; make clean ; make dbd/documentation ; make dbd/release."
	dbd/bin/package.sh $(DBD_OUT)

dbd/clean: dbd/tools/clean
	-rm $(BINDIR)/dbd_* $(SILENT_ERRORS)
	-rm $(OBJDIR)/dbd/* $(SILENT_ERRORS)

.PHONY: dbd/test
DBD_VENV=$(DBD_OUT)/dbd-venv
dbd/test:
	echo "Create a clean python environment: $(DBD_VENV)"
	-rm -rf $(DBD_VENV)
	python3 -m venv $(DBD_VENV)
	echo "Activate, install requirements and run tests"
	. $(DBD_VENV)/bin/activate && pip install -r dbd/requirements.txt && dbd/test/test-debuda-py.sh

.PHONY: dbd/coverage
dbd/coverage:
	COV=1 $(MAKE) dbd/test
	COV=1 $(MAKE) dbd/documentation
	coverage report --sort=cover
	coverage lcov # generates coverage data in the format expected by VS code "Code Coverage" extension

.PHONY: dbd/test-elf-parser
dbd/test-elf-parser:
	python3 dbd/test_parse_elf.py
	python3 dbd/test_firmware.py

.PHONY: dbd/wheel
dbd/wheel:
	python3 dbd/wheel/setup.py bdist_wheel -d build/debuda_wheel

.PHONY: dbd/wheel_release
dbd/wheel_release:
	STRIP_SYMBOLS=1 python3 dbd/wheel/setup.py bdist_wheel -d build/debuda_wheel

$(DBD_LIB): $(DBD_OBJS) $(BACKEND_LIB)
	$(PRINT_TARGET)
	@mkdir -p $(@D)
	ar rcs -o $@ $(DBD_OBJS)
	$(PRINT_OK)

$(BINDIR)/dbd_%: $(OBJDIR)/dbd/%.o $(BACKEND_LIB) $(DBD_LIB) $(VERIF_LIB)
	$(PRINT_TARGET)
	@mkdir -p $(@D)
	$(CXX) $(CFLAGS) $(CXXFLAGS) $(DBD_INCLUDES) -o $@ $^ $(LDFLAGS) $(DBD_LDFLAGS)
	$(PRINT_OK)

$(OBJDIR)/dbd/%.o: dbd/%.cpp
	$(PRINT_TARGET)
	@mkdir -p $(@D)
	$(CXX) $(DBD_CFLAGS) $(CXXFLAGS) $(STATIC_LIB_FLAGS) $(DBD_INCLUDES) $(DBD_DEFINES) -c -o $@ $<
	$(PRINT_OK)

include $(BUDA_HOME)/dbd/pybind/module.mk
include $(BUDA_HOME)/dbd/server/module.mk
