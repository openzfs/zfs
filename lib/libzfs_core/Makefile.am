include $(top_srcdir)/config/Rules.am

lib_LTLIBRARIES = libzfs_core.la

USER_C = \
	libzfs_core.c

libzfs_core_la_SOURCES = $(USER_C)

libzfs_core_la_LIBADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libzutil/libzutil.la

if BUILD_FREEBSD
libzfs_core_la_LDFLAGS = -version-info 3:0:0
libzfs_core_la_LIBADD += -lutil -lgeom
else
libzfs_core_la_LDFLAGS = -version-info 1:0:0
endif
