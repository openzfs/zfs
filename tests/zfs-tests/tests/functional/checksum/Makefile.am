include $(top_srcdir)/config/Rules.am

LDADD = \
	$(abs_top_builddir)/lib/libicp/libicp.la \
	$(abs_top_builddir)/lib/libspl/libspl_assert.la

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/checksum

dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	run_edonr_test.ksh \
	run_sha2_test.ksh \
	run_skein_test.ksh \
	filetest_001_pos.ksh \
	filetest_002_pos.ksh

dist_pkgdata_DATA = \
	default.cfg

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/checksum

pkgexec_PROGRAMS = \
	skein_test \
	sha2_test

skein_test_SOURCES = skein_test.c
sha2_test_SOURCES = sha2_test.c

if BUILD_LINUX
pkgexec_PROGRAMS += edonr_test
edonr_test_SOURCES = edonr_test.c
endif
