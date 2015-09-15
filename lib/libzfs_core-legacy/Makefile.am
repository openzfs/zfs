include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/module/zcommon

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs_core-legacy.la

USER_C = \
	libzfs_core.c

KERNEL_C = \
	zfs_prop.c \
	zpool_prop.c \
	zprop_common.c \
	zprop_conv.c

nodist_libzfs_core_legacy_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

libzfs_core_legacy_la_LIBADD = \
	-lpthread \
	$(top_builddir)/lib/libnvpair/libnvpair.la

libzfs_core_legacy_la_LDFLAGS = -version-info 1:0:0

EXTRA_DIST = $(USER_C)
