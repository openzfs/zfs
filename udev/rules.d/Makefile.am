udevrule_DATA = \
	$(top_srcdir)/udev/rules.d/60-zpool.rules \
	$(top_srcdir)/udev/rules.d/60-zvol.rules \
	$(top_srcdir)/udev/rules.d/90-zfs.rules

EXTRA_DIST = \
	$(top_srcdir)/udev/rules.d/60-zpool.rules.in \
	$(top_srcdir)/udev/rules.d/60-zvol.rules.in \
	$(top_srcdir)/udev/rules.d/90-zfs.rules.in

$(udevrule_DATA):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		'$@.in' >'$@'

distclean-local::
	-$(RM) $(udevrule_DATA)
