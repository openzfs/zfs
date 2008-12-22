include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libspl/include \
	-I${top_srcdir}/lib/libuutil/include \
	-I${top_srcdir}/lib/libzfs/include \
	-I${top_srcdir}/lib/libnvpair/include \
	-I${top_srcdir}/module/zcommon/include \
	-I${top_srcdir}/module/nvpair/include \
	-I${top_srcdir}/module/avl/include

sbin_PROGRAMS = zpool

zpool_SOURCES = \
	-I${top_srcdir}/cmd/zpool/zpool_iter.c \
	-I${top_srcdir}/cmd/zpool/zpool_main.c \
	-I${top_srcdir}/cmd/zpool/zpool_util.c \
	-I${top_srcdir}/cmd/zpool/zpool_util.h \
	-I${top_srcdir}/cmd/zpool/zpool_vdev.c

zpool_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libavl/libavl.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la

zpool_LDFLAGS = -pthread -lm -lz -lrt
