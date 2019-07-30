scriptsdir = /usr/share/initramfs-tools/scripts

scripts_DATA = \
	zfs

SUBDIRS = local-top

EXTRA_DIST = \
	$(top_srcdir)/contrib/initramfs/scripts/zfs.in

$(scripts_DATA):%:%.in
	-$(SED) -e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

clean-local::
	-$(RM) $(scripts_SCRIPTS)

distclean-local::
	-$(RM) $(scripts_SCRIPTS)
