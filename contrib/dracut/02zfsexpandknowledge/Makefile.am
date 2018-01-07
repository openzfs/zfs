pkgdracutdir = $(dracutdir)/modules.d/02zfsexpandknowledge
pkgdracut_SCRIPTS = \
	module-setup.sh

EXTRA_DIST = \
	$(top_srcdir)/contrib/dracut/02zfsexpandknowledge/module-setup.sh.in

$(pkgdracut_SCRIPTS):%:%.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@datadir\@,$(datadir),g' \
		-e 's,@dracutdir\@,$(dracutdir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

clean-local::
	-$(RM) $(pkgdracut_SCRIPTS)

distclean-local::
	-$(RM) $(pkgdracut_SCRIPTS)
