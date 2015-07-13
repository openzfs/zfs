include $(top_srcdir)/config/Rules.am

pkgdatadir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/exec

dist_pkgdata_SCRIPTS = \
	cleanup.ksh \
	setup.ksh \
	exec_001_pos.ksh \
	exec_002_neg.ksh

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/tests/functional/exec

pkgexec_PROGRAMS = mmap_exec
mmap_exec_SOURCES = mmap_exec.c
