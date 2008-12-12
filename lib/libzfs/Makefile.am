include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES = \
	-I${top_srcdir}/lib/libzfs/include \
	-I${top_srcdir}/lib/libspl/include

lib_LTLIBRARIES = libzfs.la

libzfs_la_SOURCES = \
	${top_srcdir}/lib/libzfs_changelist.c \
	${top_srcdir}/lib/libzfs_config.c \
	${top_srcdir}/lib/zfs_dataset.c \
	${top_srcdir}/lib/zfs_graph.c \
	${top_srcdir}/lib/zfs_import.c \
	${top_srcdir}/lib/libzfs_mount.c \
	${top_srcdir}/lib/libzfs_pool.c \
	${top_srcdir}/lib/libzfs_sendrecv.c \
	${top_srcdir}/lib/libzfs_status.c \
	${top_srcdir}/lib/libzfs_util.c \
	${top_srcdir}/lib/include/libzfs.h \
	${top_srcdir}/lib/include/libzfs_impl.h
