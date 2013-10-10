include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zfs

zfs_SOURCES = \
	$(top_srcdir)/cmd/zfs/zfs_iter.c \
	$(top_srcdir)/cmd/zfs/zfs_iter.h \
	$(top_srcdir)/cmd/zfs/zfs_main.c \
	$(top_srcdir)/cmd/zfs/zfs_util.h

zfs_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la

zfs_LDADD += $(ZLIB)
zfs_LDFLAGS = -pthread
