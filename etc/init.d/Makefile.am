initdir = $(DEFAULT_INIT_DIR)
init_SCRIPTS = zfs

EXTRA_DIST = \
	$(top_srcdir)/etc/init.d/zfs.fedora.in \
	$(top_srcdir)/etc/init.d/zfs.gentoo.in \
	$(top_srcdir)/etc/init.d/zfs.lsb.in \
	$(top_srcdir)/etc/init.d/zfs.lunar.in \
	$(top_srcdir)/etc/init.d/zfs.redhat.in

$(init_SCRIPTS):
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@initdir\@,$(initdir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		'$@.$(DEFAULT_INIT_SCRIPT).in' >'$@'

distclean-local::
	-$(RM) $(init_SCRIPTS)
