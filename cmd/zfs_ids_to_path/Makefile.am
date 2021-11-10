include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zfs_ids_to_path

zfs_ids_to_path_SOURCES = \
	zfs_ids_to_path.c

zfs_ids_to_path_LDADD = \
        $(abs_top_builddir)/lib/libzfs/libzfs.la

include $(top_srcdir)/config/CppCheck.am
