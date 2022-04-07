include $(top_srcdir)/config/Rules.am

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
	$(abs_top_builddir)/libzfs.la \
	$(abs_top_builddir)/libzfs_core.la \
	$(abs_top_builddir)/libnvpair.la

mount_zfs_LDADD += $(LTLIBINTL)

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += mount.zfs
