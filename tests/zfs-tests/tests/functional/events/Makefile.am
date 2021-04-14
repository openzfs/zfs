include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/events
dist_pkgdata_SCRIPTS = \
	setup.ksh \
	cleanup.ksh \
	events_001_pos.ksh \
	events_002_pos.ksh \
	zed_rc_filter.ksh \
	zed_fd_spill.ksh

dist_pkgdata_DATA = \
	events.cfg \
	events_common.kshlib

pkgexecdir = $(pkgdatadir)
pkgexec_PROGRAMS = zed_fd_spill-zedlet
zed_fd_spill_zedlet_SOURCES = zed_fd_spill-zedlet.c
