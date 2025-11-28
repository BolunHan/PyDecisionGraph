# Simple Makefile to build the docs on Linux
BUILDDIR      = build
SOURCEDIR     = source
SPHINXBUILD   = sphinx-build

.PHONY: html clean

html:
	$(SPHINXBUILD) -M html "$(SOURCEDIR)" "$(BUILDDIR)" -W
clean:
	rm -rf $(BUILDDIR)/*
