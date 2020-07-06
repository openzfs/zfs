include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/hkdf

dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	run_hkdf_test.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/hkdf

pkgexec_PROGRAMS = \
	hkdf_test

hkdf_test_SOURCES = hkdf_test.c
hkdf_test_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la
