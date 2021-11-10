include $(top_srcdir)/config/Shellcheck.am

SUBDIRS = zfs sudoers.d
SHELLCHECKDIRS = zfs
if BUILD_LINUX
SHELLCHECKDIRS += default $(ZFS_INIT_SYSV)
SUBDIRS += default $(ZFS_INIT_SYSTEMD) $(ZFS_INIT_SYSV) $(ZFS_MODULE_LOAD)
endif
DIST_SUBDIRS = default init.d zfs systemd modules-load.d sudoers.d
