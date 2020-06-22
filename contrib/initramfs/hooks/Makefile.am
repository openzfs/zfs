hooksdir = /usr/share/initramfs-tools/hooks

hooks_SCRIPTS = \
	zfs \
	zfsunlock

EXTRA_DIST = \
	zfs.in \
	zfsunlock.in

$(hooks_SCRIPTS):%:%.in Makefile
	-$(SED) -e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@mounthelperdir\@,$(mounthelperdir),g' \
		-e 's,@DEFAULT_INITCONF_DIR\@,$(DEFAULT_INITCONF_DIR),g' \
		$< >'$@'

CLEANFILES = $(hooks_SCRIPTS)
