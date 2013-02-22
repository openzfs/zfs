pkgdracutdir = $(dracutdir)/modules.d/90zfs
pkgdracut_SCRIPTS = \
	$(top_srcdir)/dracut/90zfs/module-setup.sh \
	$(top_srcdir)/dracut/90zfs/mount-zfs.sh \
	$(top_srcdir)/dracut/90zfs/parse-zfs.sh

EXTRA_DIST = \
	$(top_srcdir)/dracut/90zfs/module-setup.sh.in \
	$(top_srcdir)/dracut/90zfs/mount-zfs.sh.in \
	$(top_srcdir)/dracut/90zfs/parse-zfs.sh.in

$(pkgdracut_SCRIPTS):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		'$@.in' >'$@'

distclean-local::
	-$(RM) $(pkgdracut_SCRIPTS)
