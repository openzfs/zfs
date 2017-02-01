include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/ctime

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/ctime

pkgexec_PROGRAMS = ctime_001_pos
ctime_001_pos_SOURCES = ctime_001_pos.c
