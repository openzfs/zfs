include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

#
# Ignore the prefix for the mount helper.  It must be installed in /sbin/
# because this path is hardcoded in the mount(8) for security reasons.
#
sbindir=$(mounthelperdir)
sbin_PROGRAMS = mount.zfs

mount_zfs_SOURCES = \
	mount_zfs.c

mount_zfs_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la
