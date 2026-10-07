# Makefile for Sphinx documentation

POETRY          ?= poetry
SPHINXOPTS      ?=
SPHINXBUILD     ?= $(POETRY) run sphinx-build
SPHINXAUTOBUILD ?= $(POETRY) run sphinx-autobuild
PYTHON          ?= $(POETRY) run python
SOURCEDIR       = .
BUILDDIR        = _build

PKB_SCRIPT_DIR  ?= scripts
PKB_REPO_ROOT   ?= ..
PKB_DOC_DIR     ?= .
PKB_DOC_PATH    ?= $(CURDIR)/$(PKB_DOC_DIR)
PKB_ARGS        ?=

.PHONY: help Makefile clean deps install html html-en html-zh html-all gettext intl-update translate-zh compile-intl serve-watch pkb-check pkb-check-i18n pkb-check-links pkb-level1 pkb-review-status pkb-review-status-strict changelog-draft

help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
	@echo ""
	@echo "Custom targets:"
	@echo "  deps               Install Python (Poetry) dependencies"
	@echo "  install            Alias for deps"
	@echo "  html               Build HTML into $(BUILDDIR)/html/"
	@echo "  gettext            Generate .pot catalogs"
	@echo "  intl-update        Update zh_CN .po translation files"
	@echo "  translate-zh       Apply dictionary translations to .po files"
	@echo "  compile-intl       Compile tracked zh_CN catalogs into .mo files"
	@echo "  serve-watch        Auto-reload HTML while editing"
	@echo "  pkb-check          Zero-token staleness check"
	@echo "  pkb-check-i18n     Check zh_CN translation sync"
	@echo "  pkb-check-links    Check in-tree PKB links and numbering"
	@echo "  pkb-level1         Apply Level 1 mechanical refreshes"
	@echo "  pkb-review-status  Human review dashboard (status, score, confidentiality)"
	@echo "  pkb-review-status-strict  Fail unless every PKB page is human-approved"
	@echo "  changelog-draft    Generate changelog draft into _generated/"

deps:
	@command -v $(POETRY) >/dev/null || { \
		echo "ERROR: poetry not found. Install: pipx install poetry"; exit 1; }
	$(POETRY) install

install: deps

clean:
	rm -rf "$(BUILDDIR)"

html:
	@$(SPHINXBUILD) -M html "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

html-en:
	@rm -rf "$(BUILDDIR)/site/en"
	@SPHINX_LANGUAGE=en $(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/site/en" $(SPHINXOPTS) $(O)

html-zh:
	@rm -rf "$(BUILDDIR)/site/zh"
	@SPHINX_LANGUAGE=zh_CN $(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/site/zh" $(SPHINXOPTS) $(O)

html-all: html-en html-zh

gettext:
	@mkdir -p "$(BUILDDIR)/gettext"
	$(SPHINXBUILD) -b gettext "$(SOURCEDIR)" "$(BUILDDIR)/gettext" $(SPHINXOPTS) $(O)

intl-update: gettext
	@mkdir -p locale/zh_CN/LC_MESSAGES
	$(POETRY) run sphinx-intl update -p "$(BUILDDIR)/gettext" -l zh_CN

translate-zh: intl-update
	$(PYTHON) "$(PKB_SCRIPT_DIR)/translate_po.py" --po-dir locale/zh_CN/LC_MESSAGES

compile-intl:
	$(POETRY) run sphinx-intl build -d locale -l zh_CN

serve-watch: html
	$(SPHINXAUTOBUILD) "$(SOURCEDIR)" "$(BUILDDIR)/html" $(SPHINXOPTS) $(O) \
		--ignore "*.swp" \
		--ignore "*.pdf" \
		--ignore "*.log" \
		--ignore "*.out" \
		--ignore "*.toc" \
		--ignore "*.aux" \
		--ignore "*.idx" \
		--ignore "*.ind" \
		--ignore "*.ilg" \
		--ignore "*.tex" \
		--watch . \
		--open-browser

pkb-check:
	$(PYTHON) "$(PKB_SCRIPT_DIR)/check_pkb_staleness.py" --repo-root "$(PKB_REPO_ROOT)" --doc-dir "$(PKB_DOC_PATH)" $(PKB_ARGS)

pkb-check-i18n:
	@if [ -d "$(PKB_DOC_PATH)/locale/zh_CN/LC_MESSAGES" ]; then \
		$(PYTHON) "$(PKB_SCRIPT_DIR)/check_translation_sync.py" --repo-root "$(PKB_REPO_ROOT)" --doc-dir "$(PKB_DOC_PATH)" $(PKB_ARGS); \
	else \
		echo "No zh_CN catalogs found under $(PKB_DOC_PATH)/locale/zh_CN/LC_MESSAGES; skipping."; \
	fi

pkb-check-links:
	$(PYTHON) "$(PKB_SCRIPT_DIR)/validate_template_links.py" --template-dir "$(PKB_DOC_PATH)" --index-file "$(PKB_DOC_PATH)/index.md"

pkb-level1:
	bash "$(PKB_SCRIPT_DIR)/update_doc_level1.sh" "$(PKB_REPO_ROOT)" "$(PKB_DOC_PATH)"

pkb-review-status:
	$(PYTHON) "$(PKB_SCRIPT_DIR)/pkb_review_status.py" --doc-dir "$(PKB_DOC_PATH)"

pkb-review-status-strict:
	$(PYTHON) "$(PKB_SCRIPT_DIR)/pkb_review_status.py" --doc-dir "$(PKB_DOC_PATH)" --strict

changelog-draft:
	bash "$(PKB_SCRIPT_DIR)/gen_changelog.sh" --repo-root "$(PKB_REPO_ROOT)" --doc-dir "$(PKB_DOC_PATH)"

%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
