include $(top_srcdir)/config/Rules.am

pkgconfig_DATA = libzfs_core.pc

lib_LTLIBRARIES = libzfs_core.la

USER_C = \
	libzfs_core.c

libzfs_core_la_SOURCES = $(USER_C)

libzfs_core_la_LIBADD = \
	$(abs_top_builddir)/lib/libzutil/libzutil.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

libzfs_core_la_LIBADD += $(LTLIBINTL)

libzfs_core_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libzfs_core_la_LDFLAGS += -Wl,-z,defs
endif

if BUILD_FREEBSD
libzfs_core_la_LIBADD += -lutil -lgeom
libzfs_core_la_LDFLAGS += -version-info 3:0:0
else
libzfs_core_la_LDFLAGS += -version-info 1:0:0
endif
