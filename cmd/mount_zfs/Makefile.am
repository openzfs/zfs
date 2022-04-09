#
# Ignore the prefix for the mount helper.  It must be installed in /sbin/
# because this path is hardcoded in the mount(8) for security reasons.
# However, if needed, the configure option --with-mounthelperdir= can be used
# to override the default install location.
#
mounthelper_PROGRAMS += mount.zfs
CPPCHECKTARGETS      += mount.zfs

mount_zfs_SOURCES = \
	%D%/mount_zfs.c

mount_zfs_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la

mount_zfs_LDADD += $(LTLIBINTL)

CPPCHECKTARGETS += raidz_test
