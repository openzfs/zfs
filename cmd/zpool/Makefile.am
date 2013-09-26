include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

sbin_PROGRAMS = zpool

zpool_SOURCES = \
	$(top_srcdir)/cmd/zpool/zpool_iter.c \
	$(top_srcdir)/cmd/zpool/zpool_main.c \
	$(top_srcdir)/cmd/zpool/zpool_util.c \
	$(top_srcdir)/cmd/zpool/zpool_util.h \
	$(top_srcdir)/cmd/zpool/zpool_vdev.c

zpool_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la
