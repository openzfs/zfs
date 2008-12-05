AUTOMAKE_OPTIONS = foreign dist-zip

SUBDIRS = doc scripts $(BUILDDIR)
CONFIG_CLEAN_FILES =  aclocal.m4 config.guess config.sub
CONFIG_CLEAN_FILES += depcomp missing mkinstalldirs
EXTRA_DIST = autogen.sh

.PHONY: quilt
quilt:   .quilt-$(BUILDDIR)
autogen: .autogen-$(BUILDDIR)
config:  .config-$(BUILDDIR)
.quilt-$(BUILDDIR):
	./scripts/quilt.sh -p $(NAME) -b $(BUILDDIR) -s $(SERIESFILE) -d $(PATCHDIR)
	echo $(BUILDDIR) >$@

unquilt:
	rm -rf $(BUILDDIR)
	rm -f .quilt-$(BUILDDIR)

clean-generic:

distclean: unquilt

rpms: dist Makefile
	rpmbuild -ta $(distdir).tar.gz
