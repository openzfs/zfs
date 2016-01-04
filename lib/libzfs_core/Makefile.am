include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzfs_core.la

USER_C = \
	libzfs_core.c

KERNEL_C =

nodist_libzfs_core_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

libzfs_core_la_LIBADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la

libzfs_core_la_LDFLAGS = -version-info 1:0:0

EXTRA_DIST = $(USER_C)
