include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES = \
	-I${top_srcdir}/lib/libzfs/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libzfs.la

libzfs_la_SOURCES = \
	${top_srcdir}/lib/libzfs/libzfs_changelist.c \
	${top_srcdir}/lib/libzfs/libzfs_config.c \
	${top_srcdir}/lib/libzfs/zfs_dataset.c \
	${top_srcdir}/lib/libzfs/zfs_graph.c \
	${top_srcdir}/lib/libzfs/zfs_import.c \
	${top_srcdir}/lib/libzfs/libzfs_mount.c \
	${top_srcdir}/lib/libzfs/libzfs_pool.c \
	${top_srcdir}/lib/libzfs/libzfs_sendrecv.c \
	${top_srcdir}/lib/libzfs/libzfs_status.c \
	${top_srcdir}/lib/libzfs/libzfs_util.c \
	${top_srcdir}/lib/libzfs/include/libzfs.h \
	${top_srcdir}/lib/libzfs/include/libzfs_impl.h
