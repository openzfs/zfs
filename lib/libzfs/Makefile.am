include $(top_srcdir)/config/Rules.am

libzfs_pcdir = $(datarootdir)/pkgconfig
libzfs_pc_DATA = libzfs.pc libzfs_core.pc

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs.la

USER_C = \
	libzfs_changelist.c \
	libzfs_config.c \
	libzfs_dataset.c \
	libzfs_diff.c \
	libzfs_fru.c \
	libzfs_graph.c \
	libzfs_import.c \
	libzfs_iter.c \
	libzfs_mount.c \
	libzfs_pool.c \
	libzfs_sendrecv.c \
	libzfs_status.c \
	libzfs_util.c

KERNEL_C =

nodist_libzfs_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

libzfs_la_LIBADD = \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libshare/libshare.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzpool/libzpool.la

libzfs_la_LIBADD += -lm -ldl $(LIBBLKID)
libzfs_la_LDFLAGS = -version-info 2:0:0

EXTRA_DIST = $(libzfs_pc_DATA) $(USER_C)
