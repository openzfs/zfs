#
# Linux steals the "mount_zfs" name first, and since it's one
# giant Makefile you can no longer have two tools with the same name.
#
bin_PROGRAMS += macos_mount_zfs
CPPCHECKTARGETS += macos_mount_zfs

AM_CPPFLAGS += -I$(top_srcdir)/lib/libzfs

macos_mount_zfs_SOURCES = \
	%D%/mount_zfs.c

macos_mount_zfs_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la

macos_mount_zfs_LDADD += $(LTLIBINTL)


mount_zfs: macos_mount_zfs
	cp "$^" "$@"

INSTALL_DATA_HOOKS += mount_zfs-install-exec-hook
mount_zfs-install-exec-hook:
	cp -f $(DESTDIR)/$(sbindir)/macos_mount_zfs $(DESTDIR)/$(sbindir)/mount_zfs 
	cp -f $(DESTDIR)/$(sbindir)/mount_zfs $(FS_BUNDLEDIR)/Contents/Resources/mount_zfs
