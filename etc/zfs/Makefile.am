pkgsysconfdir = $(sysconfdir)/zfs

pkgsysconf_DATA = \
	vdev_id.conf.alias.example \
	vdev_id.conf.sas_direct.example \
	vdev_id.conf.sas_switch.example \
	vdev_id.conf.multipath.example \
	vdev_id.conf.scsi.example
pkgsysconf_SCRIPTS = \
	zfs-functions

EXTRA_DIST = $(pkgsysconf_DATA) \
	zfs-functions.in

$(pkgsysconf_SCRIPTS):%:%.in Makefile
	-(if [ -e /etc/debian_version ]; then \
		NFS_SRV=nfs-kernel-server; \
	  else \
		NFS_SRV=nfs; \
	  fi; \
	  if [ -e /sbin/openrc-run ]; then \
		SHELL=/sbin/openrc-run; \
	  else \
		SHELL=/bin/sh; \
	  fi; \
	  $(SED) \
		 -e 's,@sbindir\@,$(sbindir),g' \
		 -e 's,@sysconfdir\@,$(sysconfdir),g' \
		 -e 's,@initconfdir\@,$(initconfdir),g' \
		 $< >'$@'; \
	  [ '$@' = 'zfs-functions' ] || \
		chmod +x '$@')

CLEANFILES = $(pkgsysconf_SCRIPTS)
