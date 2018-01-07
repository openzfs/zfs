initdir = $(DEFAULT_INIT_DIR)
init_SCRIPTS = zfs-import zfs-mount zfs-share zfs-zed

initcommondir = $(sysconfdir)/zfs
initcommon_SCRIPTS = zfs-functions

initconfdir = $(DEFAULT_INITCONF_DIR)
initconf_SCRIPTS = zfs

EXTRA_DIST = \
	$(top_srcdir)/etc/init.d/zfs-functions.in \
	$(top_srcdir)/etc/init.d/zfs-share.in \
	$(top_srcdir)/etc/init.d/zfs-import.in \
	$(top_srcdir)/etc/init.d/zfs-mount.in \
	$(top_srcdir)/etc/init.d/zfs-zed.in \
	$(top_srcdir)/etc/init.d/zfs.in

$(init_SCRIPTS) $(initconf_SCRIPTS) $(initcommon_SCRIPTS):%:%.in
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
	  $(SED) -e 's,@bindir\@,$(bindir),g' \
		 -e 's,@sbindir\@,$(sbindir),g' \
		 -e 's,@udevdir\@,$(udevdir),g' \
		 -e 's,@udevruledir\@,$(udevruledir),g' \
		 -e 's,@sysconfdir\@,$(sysconfdir),g' \
		 -e 's,@initconfdir\@,$(initconfdir),g' \
		 -e 's,@initdir\@,$(initdir),g' \
		 -e 's,@runstatedir\@,$(runstatedir),g' \
		 -e "s,@SHELL\@,$$SHELL,g" \
		 -e "s,@NFS_SRV\@,$$NFS_SRV,g" \
		 $< >'$@'; \
	  [ '$@' = 'zfs-functions' -o '$@' = 'zfs' ] || \
		chmod +x '$@')

distclean-local::
	-$(RM) $(init_SCRIPTS) $(initcommon_SCRIPTS) $(initconf_SCRIPTS)
