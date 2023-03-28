pkgdracut_02dir = $(dracutdir)/modules.d/02zfsexpandknowledge
pkgdracut_02_SCRIPTS = \
	%D%/02zfsexpandknowledge/module-setup.sh

pkgdracut_90dir = $(dracutdir)/modules.d/90zfs
pkgdracut_90_SCRIPTS = \
	%D%/90zfs/export-zfs.sh \
	%D%/90zfs/import-opts-generator.sh \
	%D%/90zfs/module-setup.sh \
	%D%/90zfs/mount-zfs.sh \
	%D%/90zfs/parse-zfs.sh \
	%D%/90zfs/zfs-generator.sh \
	%D%/90zfs/zfs-lib.sh \
	%D%/90zfs/zfs-load-key.sh \
	%D%/90zfs/zfs-needshutdown.sh

pkgdracut_90_DATA = \
	%D%/90zfs/zfs-env-bootfs.service \
	%D%/90zfs/zfs-nonroot-necessities.service \
	%D%/90zfs/zfs-rollback-bootfs.service \
	%D%/90zfs/zfs-snapshot-bootfs.service

SUBSTFILES        += $(pkgdracut_02_SCRIPTS) $(pkgdracut_90_SCRIPTS) $(pkgdracut_90_DATA)
SHELLCHECKSCRIPTS += $(pkgdracut_02_SCRIPTS) $(pkgdracut_90_SCRIPTS)
# Provided by /bin/sleep, and, again, every implementation of that supports this
$(call SHELLCHECK_OPTS,$(pkgdracut_90_SCRIPTS)): CHECKBASHISMS_IGNORE = -e 'sleep only takes one integer' -e 'sleep 0.'

dist_noinst_DATA += %D%/README.md
