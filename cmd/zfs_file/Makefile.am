include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zfs_file

zfs_file_SOURCES = \
	zfs_file.c

zfs_file_LDADD = \
        $(abs_top_builddir)/lib/libzfs/libzfs.la \
        $(abs_top_builddir)/lib/libzfs_core/libzfs_core.la
