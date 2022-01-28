include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/suid

dist_pkgdata_SCRIPTS = \
	suid_write_to_suid.ksh \
	suid_write_to_sgid.ksh \
	suid_write_to_suid_sgid.ksh \
	suid_write_to_none.ksh \
	suid_write_zil_replay.ksh \
	cleanup.ksh \
	setup.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/suid

pkgexec_PROGRAMS = suid_write_to_file
suid_write_to_file_SOURCES = suid_write_to_file.c
