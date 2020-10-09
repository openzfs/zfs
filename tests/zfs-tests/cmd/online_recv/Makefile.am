include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

pkgexec_PROGRAMS = online_recv

online_recv_SOURCES = online_recv.c
online_recv_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la
