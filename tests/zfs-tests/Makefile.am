SUBDIRS += %D%/tests

include $(srcdir)/%D%/cmd/Makefile.am


scripts_zfs_tests_functional_libzfsdir = $(datadir)/$(PACKAGE)/zfs-tests/tests/functional/libzfs
scripts_zfs_tests_functional_libzfs_PROGRAMS = %D%/tests/functional/libzfs/many_fds
%C%_tests_functional_libzfs_many_fds_LDADD = \
	libzfs.la

scripts_zfs_tests_functional_hkdfdir = $(datadir)/$(PACKAGE)/zfs-tests/tests/functional/hkdf
scripts_zfs_tests_functional_hkdf_PROGRAMS = %D%/tests/functional/hkdf/hkdf_test
%C%_tests_functional_hkdf_hkdf_test_LDADD = \
	libzpool.la

if BUILD_LINUX
scripts_zfs_tests_functional_tmpfiledir = $(datadir)/$(PACKAGE)/zfs-tests/tests/functional/tmpfile
scripts_zfs_tests_functional_tmpfile_PROGRAMS = \
	%D%/tests/functional/tmpfile/tmpfile_001_pos \
	%D%/tests/functional/tmpfile/tmpfile_002_pos \
	%D%/tests/functional/tmpfile/tmpfile_003_pos \
	%D%/tests/functional/tmpfile/tmpfile_stat_mode \
	%D%/tests/functional/tmpfile/tmpfile_test
endif


scripts_zfs_tests_callbacksdir = $(datadir)/$(PACKAGE)/zfs-tests/callbacks
dist_scripts_zfs_tests_callbacks_SCRIPTS = \
	%D%/callbacks/zfs_dbgmsg.ksh \
	%D%/callbacks/zfs_dmesg.ksh \
	%D%/callbacks/zfs_failsafe.ksh \
	%D%/callbacks/zfs_mmp.ksh


scripts_zfs_tests_includedir = $(datadir)/$(PACKAGE)/zfs-tests/include
dist_scripts_zfs_tests_include_DATA = \
	%D%/include/blkdev.shlib \
	%D%/include/commands.cfg \
	%D%/include/libtest.shlib \
	%D%/include/math.shlib \
	%D%/include/properties.shlib \
	%D%/include/tunables.cfg \
	%D%/include/zpool_script.shlib

nodist_scripts_zfs_tests_include_DATA = \
	%D%/include/default.cfg

SUBSTFILES += $(nodist_scripts_zfs_tests_include_DATA)
