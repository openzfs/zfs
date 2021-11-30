include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

pkgdracutdir = $(dracutdir)/modules.d/90zfs
pkgdracut_SCRIPTS = \
	export-zfs.sh \
	module-setup.sh \
	mount-zfs.sh \
	parse-zfs.sh \
	zfs-generator.sh \
	zfs-load-key.sh \
	zfs-needshutdown.sh \
	zfs-lib.sh \
	import-opts-generator.sh

pkgdracut_DATA = \
	zfs-env-bootfs.service \
	zfs-snapshot-bootfs.service \
	zfs-rollback-bootfs.service

SUBSTFILES += $(pkgdracut_SCRIPTS) $(pkgdracut_DATA)
SHELLCHECK_OPTS = --enable=all

# Provided by /bin/sleep, and, again, every implementation of that supports this
CHECKBASHISMS_IGNORE = -e 'sleep only takes one integer' -e 'sleep 0.'
