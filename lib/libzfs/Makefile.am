include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs.la

libzfs_la_LDFLAGS = -lm -ldl $(LIBSELINUX)

libzfs_la_LIBADD = \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libefi/libefi.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la

libzfs_la_SOURCES = \
	$(top_srcdir)/lib/libzfs/libzfs_changelist.c \
	$(top_srcdir)/lib/libzfs/libzfs_config.c \
	$(top_srcdir)/lib/libzfs/libzfs_dataset.c \
	$(top_srcdir)/lib/libzfs/libzfs_diff.c \
	$(top_srcdir)/lib/libzfs/libzfs_fru.c \
	$(top_srcdir)/lib/libzfs/libzfs_graph.c \
	$(top_srcdir)/lib/libzfs/libzfs_import.c \
	$(top_srcdir)/lib/libzfs/libzfs_mount.c \
	$(top_srcdir)/lib/libzfs/libzfs_pool.c \
	$(top_srcdir)/lib/libzfs/libzfs_sendrecv.c \
	$(top_srcdir)/lib/libzfs/libzfs_status.c \
	$(top_srcdir)/lib/libzfs/libzfs_util.c
