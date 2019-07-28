hooksdir = /usr/share/initramfs-tools/hooks

hooks_SCRIPTS = \
	zfs

EXTRA_DIST = \
	$(top_srcdir)/contrib/initramfs/hooks/zfs.in

$(hooks_SCRIPTS):%:%.in
	-$(SED) -e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@mounthelperdir\@,$(mounthelperdir),g' \
		$< >'$@'

clean-local::
	-$(RM) $(hooks_SCRIPTS)

distclean-local::
	-$(RM) $(hooks_SCRIPTS)
