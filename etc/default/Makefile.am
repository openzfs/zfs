initconfdir = $(DEFAULT_INITCONF_DIR)
initconf_SCRIPTS = zfs

EXTRA_DIST = \
	zfs.in

$(initconf_SCRIPTS):%:%.in Makefile
	  $(SED) \
		 -e 's,@sysconfdir\@,$(sysconfdir),g' \
		 $< >'$@'

CLEANFILES = $(initconf_SCRIPTS)
