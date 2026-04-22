# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line.
TAGS          =
SPHINXOPTS    = -D breathe_projects.media_library=$(MEDIA_LIBRARY_DOXYGEN_XML_PATH) -D breathe_projects.hailo_analytics=$(HAILO_ANALYTICS_DOXYGEN_XML_PATH) -W --keep-going # https://www.sphinx-doc.org/en/master/man/sphinx-build.html
SPHINXBUILD   = sphinx-build
SPHINXPROJ    = MediaLibrary
SOURCEDIR     = source
BUILDDIR      = build

ifeq ($(INTERNAL), 1)
TAGS = -t internal
endif

# Put it first so that "make" without argument is like "make help".
help:
	$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

# Latex has its own rule to support numfig
latex:
	$(SPHINXBUILD) -M latex "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) -D numfig=1 $(TAGS) $(O)

.PHONY: help latex Makefile

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile
	$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(TAGS) $(O)
