DOCS_PUBLIC_DIR = $(DOCSDIR)/public
DOCS_PUBLIC_SRC_DIR = docs/public
DOCS_PUBLIC_SRCS = $(shell find $(DOCS_PUBLIC_SRC_DIR) -type f \( -name '*.md' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.dox' -o -name '*.html' -o -name '*.tex' \))
DOCS_PUBLIC_BUILD_SCRIPT = docs/public/build.sh

docs/public: $(DOCS_PUBLIC_DIR)

$(DOCS_PUBLIC_DIR): $(DOCS_PUBLIC_BUILD_SCRIPT) $(DOCS_PUBLIC_SRCS)
	install -d $(DOCS_PUBLIC_DIR)
	BUILD_DIR=$(DOCS_PUBLIC_DIR) \
	SOURCE_DIR=$(DOCS_PUBLIC_SRC_DIR) \
	$(DOCS_PUBLIC_BUILD_SCRIPT)

docs/public/publish: docs/public
	rsync --delete -avz $(DOCS_PUBLIC_DIR)/html/ yyz-webservice-02:/var/www/html/docs/budabackend-docs

docs/public/clean:
	rm -rf $(DOCS_PUBLIC_DIR)/html
	rm -rf $(DOCS_PUBLIC_DIR)/xml
	rm -rf $(DOCS_PUBLIC_DIR)/latex
	rm -rf $(DOCS_PUBLIC_DIR)/*.pdf
