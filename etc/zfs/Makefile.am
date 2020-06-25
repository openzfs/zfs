pkgsysconfdir = $(sysconfdir)/zfs
initconfdir = $(DEFAULT_INITCONF_DIR)

dist_pkgsysconf_DATA = \
	vdev_id.conf.alias.example \
	vdev_id.conf.sas_direct.example \
	vdev_id.conf.sas_switch.example \
	vdev_id.conf.multipath.example \
	vdev_id.conf.scsi.example
pkgsysconf_SCRIPTS = \
	zfs-functions

EXTRA_DIST = zfs-functions.in

$(pkgsysconf_SCRIPTS):%:%.in Makefile
	  -$(SED) \
		 -e 's,@sbindir\@,$(sbindir),g' \
		 -e 's,@sysconfdir\@,$(sysconfdir),g' \
		 -e 's,@initconfdir\@,$(initconfdir),g' \
		 $< >'$@'

CLEANFILES = $(pkgsysconf_SCRIPTS)
