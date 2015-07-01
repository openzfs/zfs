include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/threadsappend

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh \
	threadsappend_001_pos.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/threadsappend

pkgexec_PROGRAMS = threadsappend
threadsappend_SOURCES = threadsappend.c
threadsappend_LDADD = -lpthread
