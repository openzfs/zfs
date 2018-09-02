include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/libzfs

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/libzfs

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh \
	libzfs_input.ksh

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

many_fds_LDADD = \
	$(top_builddir)/lib/libzfs/libzfs.la

pkgexec_PROGRAMS = many_fds
many_fds_SOURCES = many_fds.c

