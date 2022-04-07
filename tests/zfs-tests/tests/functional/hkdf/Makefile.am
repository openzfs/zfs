include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/hkdf

pkgexec_PROGRAMS = hkdf_test
hkdf_test_SOURCES = hkdf_test.c
hkdf_test_LDADD = \
	$(abs_top_builddir)/libzpool.la
