scriptsdir = $(datarootdir)/initramfs-tools/scripts

scripts_DATA = \
	zfs

SUBDIRS = local-top

EXTRA_DIST = \
	$(top_srcdir)/contrib/initramfs/scripts/zfs.in

$(scripts_DATA):%:%.in
	-$(SED) -e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

# Double-colon rules are allowed; there are multiple independent definitions.
clean-local::
	-$(RM) $(scripts_SCRIPTS)

# Double-colon rules are allowed; there are multiple independent definitions.
distclean-local::
	-$(RM) $(scripts_SCRIPTS)
