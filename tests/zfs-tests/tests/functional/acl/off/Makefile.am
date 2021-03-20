include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/acl/off

dist_pkgdata_SCRIPTS = \
	dosmode.ksh \
	posixmode.ksh \
	cleanup.ksh \
	setup.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/acl/off

if BUILD_FREEBSD
pkgexec_PROGRAMS = dosmode_readonly_write
dosmode_readonly_write_SOURCES = dosmode_readonly_write.c
endif
