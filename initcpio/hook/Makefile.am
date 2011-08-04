initcpiohookdir = $(initcpiodir)/hooks

initcpiohook_SCRIPTS = zfs

EXTRA_DIST = zfs.in

$(initcpiohook_SCRIPTS):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' '$@.in' >'$@'

distclean-local::
	-$(RM) $(initcpiohook_SCRIPTS)

