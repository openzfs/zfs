pkgfstabdir = $(datadir)/@PACKAGE@
pkgfstab_SCRIPTS = \
	fstab-generator.sh

EXTRA_DIST = \
	$(top_srcdir)/contrib/fstab-generator/fstab-generator.sh.in \
	$(top_srcdir)/contrib/fstab-generator/README.fstab-generator.markdown

$(pkgfstab_SCRIPTS):%:%.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

distclean-local::
	-$(RM) $(pkgfstab_SCRIPTS)
