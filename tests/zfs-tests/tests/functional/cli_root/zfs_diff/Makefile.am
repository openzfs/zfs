include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cli_root/zfs_diff

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh \
	zfs_diff_changes.ksh \
	zfs_diff_cliargs.ksh \
	zfs_diff_encrypted.ksh \
	zfs_diff_timestamp.ksh \
	zfs_diff_types.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/cli_root/zfs_diff

pkgexec_PROGRAMS = socket
socket_SOURCES = socket.c
