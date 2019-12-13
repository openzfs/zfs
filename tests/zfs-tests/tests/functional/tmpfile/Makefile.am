include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/tmpfile

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/tmpfile

pkgexec_PROGRAMS = tmpfile_test tmpfile_001_pos tmpfile_002_pos \
	tmpfile_003_pos tmpfile_stat_mode
tmpfile_test_SOURCES= tmpfile_test.c
tmpfile_001_pos_SOURCES = tmpfile_001_pos.c
tmpfile_002_pos_SOURCES = tmpfile_002_pos.c
tmpfile_003_pos_SOURCES = tmpfile_003_pos.c
