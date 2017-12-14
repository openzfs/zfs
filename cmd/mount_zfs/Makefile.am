include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

#
# Ignore the prefix for the mount helper.  It must be installed in /sbin/
# because this path is hardcoded in the mount(8) for security reasons.
# However, if needed, the configure option --with-mounthelperdir= can be used
# to override the default install location.
#
sbindir=$(mounthelperdir)
sbin_PROGRAMS = mount.zfs

mount_zfs_SOURCES = \
	mount_zfs.c

mount_zfs_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs/libzfs.la
