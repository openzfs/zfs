include $(top_srcdir)/config/Substfiles.am
include $(top_srcdir)/config/Shellcheck.am

pkgsysconfdir = $(sysconfdir)/zfs

dist_pkgsysconf_DATA = \
	vdev_id.conf.alias.example \
	vdev_id.conf.sas_direct.example \
	vdev_id.conf.sas_switch.example \
	vdev_id.conf.multipath.example \
	vdev_id.conf.scsi.example

pkgsysconf_SCRIPTS = \
	zfs-functions

SUBSTFILES += $(pkgsysconf_SCRIPTS)

SHELLCHECK_OPTS = --enable=all
SHELLCHECK_SHELL = dash
