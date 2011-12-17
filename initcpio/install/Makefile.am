initcpioinstalldir = $(initcpiodir)/install

initcpioinstall_SCRIPTS = zfs

EXTRA_DIST = zfs.in

$(initcpioinstall_SCRIPTS):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' '$@.in' >'$@'

distclean-local::
	-$(RM) $(initcpioinstall_SCRIPTS)

