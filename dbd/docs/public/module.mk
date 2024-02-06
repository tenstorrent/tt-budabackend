# Build the docs from root directory with: make dbd/docs/public
# Publish them to http://yyz-webservice-02.local.tenstorrent.com/docs/debuda-docs/ with: make dbd/docs/public/publish
DBD_DOCS_PUBLIC_DIR = dbd/docs/public
DBD_DOCS_PUBLIC_SRCS_RST = $(shell find $(DBD_DOCS_PUBLIC_DIR) -type f -name '*.rst')
DBD_DOCS_PUBLIC_SRCS_MD = $(shell find $(DBD_DOCS_PUBLIC_DIR) -type f -name '*.md')
DBD_DOCS_PUBLIC_SRCS=$(DBD_DOCS_PUBLIC_SRCS_RST) $(DBD_DOCS_PUBLIC_SRCS_MD)
DBD_DOCS_PUBLIC_SPHINX_BUILDER = html
DBD_DOCS_PUBLIC_BUILD_SCRIPT = $(DBD_DOCS_PUBLIC_DIR)/build.sh

.PHONY: $(DBD_DOCS_PUBLIC_DIR)
$(DBD_DOCS_PUBLIC_DIR): $(DBD_DOCS_PUBLIC_BUILD_SCRIPT) $(DBD_DOCS_PUBLIC_SRCS)
	$(PRINT_TARGET)
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(LIBDIR) \
	BUILDER=$(DBD_DOCS_PUBLIC_SPHINX_BUILDER) \
	SOURCE_DIR=$(DBD_DOCS_PUBLIC_DIR) \
	INSTALL_DIR=$(@)/build \
	LANG=C.UTF-8 \
	LC_ALL=C.UTF-8 \
	$(DBD_DOCS_PUBLIC_BUILD_SCRIPT)
	$(PRINT_OK)

.PHONY: $(DBD_DOCS_PUBLIC_DIR)/publish
$(DBD_DOCS_PUBLIC_DIR)/publish:
	$(PRINT_TARGET)
	rsync -e "ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no" --delete -avz  $(DBD_DOCS_PUBLIC_DIR)/build/html/ yyz-webservice-02:/var/www/html/docs/debuda-docs
	$(PRINT_OK)

.PHONY: $(DBD_DOCS_PUBLIC_DIR)/clean
$(DBD_DOCS_PUBLIC_DIR)/clean:
	rm -rf $(DBD_DOCS_PUBLIC_DIR)/build
