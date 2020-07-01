pkgdracutdir = $(dracutdir)/modules.d/90zfs
pkgdracut_SCRIPTS = \
	export-zfs.sh \
	module-setup.sh \
	mount-zfs.sh \
	parse-zfs.sh \
	zfs-generator.sh \
	zfs-load-key.sh \
	zfs-needshutdown.sh \
	zfs-lib.sh

pkgdracut_DATA = \
	zfs-env-bootfs.service \
	zfs-snapshot-bootfs.service \
	zfs-rollback-bootfs.service

EXTRA_DIST = \
	export-zfs.sh.in \
	module-setup.sh.in \
	mount-zfs.sh.in \
	parse-zfs.sh.in \
	zfs-generator.sh.in \
	zfs-load-key.sh.in \
	zfs-needshutdown.sh.in \
	zfs-lib.sh.in \
	zfs-env-bootfs.service.in \
	zfs-snapshot-bootfs.service.in \
	zfs-rollback-bootfs.service.in

$(pkgdracut_SCRIPTS) $(pkgdracut_DATA) :%:%.in
	-$(SED) -e 's,@bindir\@,$(bindir),g' \
		-e 's,@sbindir\@,$(sbindir),g' \
		-e 's,@udevdir\@,$(udevdir),g' \
		-e 's,@udevruledir\@,$(udevruledir),g' \
		-e 's,@sysconfdir\@,$(sysconfdir),g' \
		-e 's,@systemdunitdir\@,$(systemdunitdir),g' \
		-e 's,@mounthelperdir\@,$(mounthelperdir),g' \
		$< >'$@'

CLEANFILES = $(pkgdracut_SCRIPTS) $(pkgdracut_DATA)
