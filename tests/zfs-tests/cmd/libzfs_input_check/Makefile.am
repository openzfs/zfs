include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = libzfs_input_check

if BUILD_FREEBSD
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/freebsd/zfs
endif
if BUILD_LINUX
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/linux/zfs
endif

libzfs_input_check_SOURCES = libzfs_input_check.c
libzfs_input_check_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la
