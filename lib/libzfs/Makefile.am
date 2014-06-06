include $(top_srcdir)/config/Rules.am

libzfs_pcdir = $(datarootdir)/pkgconfig
libzfs_pc_DATA = $(top_srcdir)/lib/libzfs/libzfs.pc $(top_srcdir)/lib/libzfs/libzfs_core.pc
EXTRA_DIST = $(top_srcdir)/lib/libzfs/libzfs.pc $(top_srcdir)/lib/libzfs/libzfs_core.pc

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs.la

libzfs_la_SOURCES = \
	$(top_srcdir)/lib/libzfs/libzfs_changelist.c \
	$(top_srcdir)/lib/libzfs/libzfs_config.c \
	$(top_srcdir)/lib/libzfs/libzfs_dataset.c \
	$(top_srcdir)/lib/libzfs/libzfs_diff.c \
	$(top_srcdir)/lib/libzfs/libzfs_fru.c \
	$(top_srcdir)/lib/libzfs/libzfs_graph.c \
	$(top_srcdir)/lib/libzfs/libzfs_import.c \
	$(top_srcdir)/lib/libzfs/libzfs_iter.c \
	$(top_srcdir)/lib/libzfs/libzfs_mount.c \
	$(top_srcdir)/lib/libzfs/libzfs_pool.c \
	$(top_srcdir)/lib/libzfs/libzfs_sendrecv.c \
	$(top_srcdir)/lib/libzfs/libzfs_status.c \
	$(top_srcdir)/lib/libzfs/libzfs_util.c

libzfs_la_LIBADD = \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libshare/libshare.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzpool/libzpool.la

libzfs_la_LIBADD += -lm -ldl $(LIBBLKID)
libzfs_la_LDFLAGS = -version-info 2:0:0
