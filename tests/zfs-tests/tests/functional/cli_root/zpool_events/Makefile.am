include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cli_root/zpool_events
pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cli_root/zpool_events

dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	zpool_events_clear.ksh \
	zpool_events_cliargs.ksh \
	zpool_events_follow.ksh \
	zpool_events_poolname.ksh \
	zpool_events_errors.ksh \
	zpool_events_duplicates.ksh

dist_pkgdata_DATA = \
	zpool_events.cfg \
	zpool_events.kshlib

ereports_LDADD = \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libzfs/libzfs.la

pkgexec_PROGRAMS = ereports
ereports_SOURCES = ereports.c
