SUBDIRS = zfs sudoers.d
if BUILD_LINUX
SUBDIRS += default $(ZFS_INIT_SYSTEMD) $(ZFS_INIT_SYSV) $(ZFS_MODULE_LOAD)
endif
if BUILD_FREEBSD
SUBDIRS += rc.d
endif
DIST_SUBDIRS = default init.d zfs systemd modules-load.d rc.d sudoers.d
