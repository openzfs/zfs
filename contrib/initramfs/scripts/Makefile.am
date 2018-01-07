scriptsdir = $(datarootdir)/initramfs-tools/scripts

scripts_SCRIPTS = \
	zfs

SUBDIRS = local-top

EXTRA_DIST = \
	$(top_srcdir)/contrib/initramfs/scripts/zfs.in

$(scripts_SCRIPTS):%:%.in
	-$(SED) -e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

clean-local::
	-$(RM) $(scripts_SCRIPTS)

distclean-local::
	-$(RM) $(scripts_SCRIPTS)
