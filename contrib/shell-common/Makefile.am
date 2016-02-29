zfsfuncsdir = $(sysconfdir)/zfs
zfsfuncs_SCRIPTS = zfs-functions

defconfdir = $(DEFAULT_INITCONF_DIR)
defconf_SCRIPTS = zfs

EXTRA_DIST = \
	$(top_srcdir)/contrib/shell-common/zfs-functions.in \
	$(top_srcdir)/contrib/shell-common/zfs.in

$(defconf_SCRIPTS) $(zfsfuncs_SCRIPTS): $(EXTRA_DIST)
	-($(SED) -e 's,@bindir\@,$(bindir),g' \
		 -e 's,@sbindir\@,$(sbindir),g' \
		 -e 's,@udevdir\@,$(udevdir),g' \
		 -e 's,@udevruledir\@,$(udevruledir),g' \
		 -e 's,@sysconfdir\@,$(sysconfdir),g' \
		 -e 's,@initconfdir\@,$(defconfdir),g' \
		 -e 's,@initdir\@,$(initdir),g' \
		 -e 's,@runstatedir\@,$(runstatedir),g' \
		 '$@.in' >'$@')

distclean-local::
	-$(RM) $(defconf_SCRIPTS)  $(zfsfuncs_SCRIPTS)
