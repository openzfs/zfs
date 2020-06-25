systemdgenerator_SCRIPTS = \
	zfs-mount-generator

EXTRA_DIST = \
	zfs-mount-generator.in

$(systemdgenerator_SCRIPTS): %: %.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@runstatedir\@,$(runstatedir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		$< >'$@'

CLEANFILES = $(systemdgenerator_SCRIPTS)
