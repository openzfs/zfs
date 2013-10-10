include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs_core.la

libzfs_core_la_SOURCES = \
	$(top_srcdir)/lib/libzfs_core/libzfs_core.c

libzfs_core_la_LIBADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la

libzfs_core_la_LDFLAGS = -version-info 1:0:0
