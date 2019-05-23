include $(top_srcdir)/config/Rules.am

AM_CPPFLAGS += -I$(top_srcdir)/include
AM_CPPFLAGS += -I$(top_srcdir)/lib/libspl/include
LDADD = $(top_builddir)/lib/libzpool/libzpool.la

AUTOMAKE_OPTIONS = subdir-objects

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/hkdf

dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	run_hkdf_test.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/hkdf

pkgexec_PROGRAMS = \
	hkdf_test

hkdf_test_SOURCES = hkdf_test.c
