include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cp_files

dist_pkgdata_SCRIPTS = \
	cp_files_001_pos.ksh \
	cp_stress.ksh \
	cleanup.ksh \
	setup.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cp_files

pkgexec_PROGRAMS = cp_files seekflood
cp_files_SOURCES= cp_files.c
seekflood_SOURCES = seekflood.c
