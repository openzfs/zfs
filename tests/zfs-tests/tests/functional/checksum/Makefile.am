include $(top_srcdir)/config/Rules.am

AM_CPPFLAGS += -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libicp/libicp.la

AUTOMAKE_OPTIONS = subdir-objects

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/checksum

dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	run_edonr_test.ksh \
	run_sha2_test.ksh \
	run_skein_test.ksh \
	filetest_001_pos.ksh

dist_pkgdata_DATA = \
	default.cfg

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/checksum

pkgexec_PROGRAMS = \
	edonr_test \
	skein_test \
	sha2_test

edonr_test_SOURCES = edonr_test.c
skein_test_SOURCES = skein_test.c
sha2_test_SOURCES = sha2_test.c
