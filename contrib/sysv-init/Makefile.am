initdir = $(DEFAULT_INIT_DIR)
init_SCRIPTS = zfs-import zfs-mount zfs-share zfs-zed

EXTRA_DIST = \
	$(top_srcdir)/contrib/sysv-init/zfs-share.in \
	$(top_srcdir)/contrib/sysv-init/zfs-import.in \
	$(top_srcdir)/contrib/sysv-init/zfs-mount.in \
	$(top_srcdir)/contrib/sysv-init/zfs-zed.in

$(init_SCRIPTS): $(EXTRA_DIST)
	-(if [ -e /etc/debian_version ]; then \
		NFS_SRV=nfs-kernel-server; \
	  else \
		NFS_SRV=nfs; \
	  fi; \
	  if [ -e /etc/gentoo-release ]; then \
		SHELL=/sbin/runscript; \
	  else \
		SHELL=/bin/sh; \
	  fi; \
	  $(SED) -e 's,@bindir\@,$(bindir),g' \
		 -e 's,@sbindir\@,$(sbindir),g' \
		 -e 's,@udevdir\@,$(udevdir),g' \
		 -e 's,@udevruledir\@,$(udevruledir),g' \
		 -e 's,@sysconfdir\@,$(sysconfdir),g' \
		 -e 's,@initconfdir\@,$(DEFAULT_INITCONF_DIR),g' \
		 -e 's,@initdir\@,$(initdir),g' \
		 -e 's,@runstatedir\@,$(runstatedir),g' \
		 -e "s,@SHELL\@,$$SHELL,g" \
		 -e "s,@NFS_SRV\@,$$NFS_SRV,g" \
		 "$(top_srcdir)/contrib/sysv-init/$@.in" >'$@'; \
	  [ '$@' = 'zfs-functions' -o '$@' = 'zfs' ] || \
		chmod +x '$@')

distclean-local::
	-$(RM) $(init_SCRIPTS) $(initconf_SCRIPTS)
