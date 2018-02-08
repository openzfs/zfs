include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs_core.la

USER_C = \
	libzfs_core.c

nodist_libzfs_core_la_SOURCES = $(USER_C)

libzfs_core_la_LIBADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la

libzfs_core_la_LDFLAGS = -version-info 1:0:0

EXTRA_DIST = $(USER_C)
