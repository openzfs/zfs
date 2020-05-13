include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/libzfs

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/libzfs

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh \
	libzfs_input.ksh

many_fds_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libzfs/libzfs.la

pkgexec_PROGRAMS = many_fds
many_fds_SOURCES = many_fds.c

