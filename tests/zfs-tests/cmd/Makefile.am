scripts_zfs_tests_bindir = $(datadir)/$(PACKAGE)/zfs-tests/bin


scripts_zfs_tests_bin_PROGRAMS  = %D%/chg_usr_exec
scripts_zfs_tests_bin_PROGRAMS += %D%/clonefile
scripts_zfs_tests_bin_PROGRAMS += %D%/cp_files
scripts_zfs_tests_bin_PROGRAMS += %D%/ctime
scripts_zfs_tests_bin_PROGRAMS += %D%/dir_rd_update
scripts_zfs_tests_bin_PROGRAMS += %D%/dosmode_readonly_write
scripts_zfs_tests_bin_PROGRAMS += %D%/get_diff
scripts_zfs_tests_bin_PROGRAMS += %D%/rename_dir
scripts_zfs_tests_bin_PROGRAMS += %D%/suid_write_to_file
scripts_zfs_tests_bin_PROGRAMS += %D%/truncate_test
scripts_zfs_tests_bin_PROGRAMS += %D%/zfs_diff-socket


scripts_zfs_tests_bin_PROGRAMS += %D%/badsend
%C%_badsend_LDADD = \
	libzfs_core.la \
	libzfs.la \
	libnvpair.la


scripts_zfs_tests_bin_PROGRAMS += %D%/btree_test
%C%_btree_test_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)
%C%_btree_test_LDADD = \
	libzpool.la \
	libzfs_core.la


if WANT_DEVNAME2DEVID
scripts_zfs_tests_bin_PROGRAMS += %D%/devname2devid
%C%_devname2devid_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS)
%C%_devname2devid_LDADD = $(LIBUDEV_LIBS)
endif


scripts_zfs_tests_bin_PROGRAMS += %D%/draid
%C%_draid_CFLAGS = $(AM_CFLAGS) $(ZLIB_CFLAGS)
%C%_draid_LDADD = \
	libzpool.la \
	libnvpair.la
%C%_draid_LDADD += $(ZLIB_LIBS)

dist_noinst_DATA += %D%/file/file_common.h
scripts_zfs_tests_bin_PROGRAMS += %D%/file_append %D%/file_check %D%/file_trunc %D%/file_write %D%/largest_file %D%/randwritecomp
%C%_file_append_SOURCES   = %D%/file/file_append.c
%C%_file_check_SOURCES    = %D%/file/file_check.c
%C%_file_trunc_SOURCES    = %D%/file/file_trunc.c
%C%_file_write_SOURCES    = %D%/file/file_write.c
%C%_largest_file_SOURCES  = %D%/file/largest_file.c
%C%_randwritecomp_SOURCES = %D%/file/randwritecomp.c


scripts_zfs_tests_bin_PROGRAMS += %D%/libzfs_input_check
%C%_libzfs_input_check_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include/os/@ac_system_l@/zfs
%C%_libzfs_input_check_LDADD = \
	libzfs_core.la \
	libnvpair.la


scripts_zfs_tests_bin_PROGRAMS += %D%/mkbusy %D%/mkfile %D%/mkfiles %D%/mktree
%C%_mkfile_LDADD = $(LTLIBINTL)


scripts_zfs_tests_bin_PROGRAMS += %D%/mmap_exec %D%/mmap_seek %D%/mmap_sync %D%/mmapwrite %D%/readmmap
%C%_mmapwrite_LDADD = -lpthread

if WANT_MMAP_LIBAIO
scripts_zfs_tests_bin_PROGRAMS += %D%/mmap_libaio
%C%_mmap_libaio_CFLAGS = $(AM_CFLAGS) $(LIBAIO_CFLAGS)
%C%_mmap_libaio_LDADD  = $(LIBAIO_LIBS)
endif


scripts_zfs_tests_bin_PROGRAMS += %D%/nvlist_to_lua
%C%_nvlist_to_lua_LDADD = \
	libzfs_core.la \
	libnvpair.la

scripts_zfs_tests_bin_PROGRAMS += %D%/rm_lnkcnt_zero_file
%C%_rm_lnkcnt_zero_file_LDADD = -lpthread

scripts_zfs_tests_bin_PROGRAMS += %D%/send_doall
%C%_send_doall_LDADD = \
	libzfs_core.la \
	libzfs.la \
	libnvpair.la

scripts_zfs_tests_bin_PROGRAMS += %D%/stride_dd
%C%_stride_dd_LDADD = -lrt

scripts_zfs_tests_bin_PROGRAMS += %D%/threadsappend
%C%_threadsappend_LDADD = -lpthread

scripts_zfs_tests_bin_PROGRAMS += %D%/ereports
%C%_ereports_LDADD = \
	libnvpair.la \
	libzfs.la


scripts_zfs_tests_bin_PROGRAMS += %D%/edonr_test %D%/skein_test \
	%D%/sha2_test %D%/blake3_test
%C%_skein_test_SOURCES = %D%/checksum/skein_test.c
%C%_sha2_test_SOURCES  = %D%/checksum/sha2_test.c
%C%_edonr_test_SOURCES = %D%/checksum/edonr_test.c
%C%_blake3_test_SOURCES = %D%/checksum/blake3_test.c
%C%_skein_test_LDADD = \
	libicp.la \
	libspl.la \
	libspl_assert.la
%C%_sha2_test_LDADD  = $(%C%_skein_test_LDADD)
%C%_edonr_test_LDADD = $(%C%_skein_test_LDADD)
%C%_blake3_test_LDADD = $(%C%_skein_test_LDADD)

if BUILD_LINUX
scripts_zfs_tests_bin_PROGRAMS += %D%/getversion
scripts_zfs_tests_bin_PROGRAMS += %D%/user_ns_exec
scripts_zfs_tests_bin_PROGRAMS += %D%/renameat2
scripts_zfs_tests_bin_PROGRAMS += %D%/xattrtest
scripts_zfs_tests_bin_PROGRAMS += %D%/zed_fd_spill-zedlet
scripts_zfs_tests_bin_PROGRAMS += %D%/idmap_util

%C%_idmap_util_LDADD = libspl.la

dist_noinst_DATA += %D%/linux_dos_attributes/dos_attributes.h
scripts_zfs_tests_bin_PROGRAMS  += %D%/read_dos_attributes %D%/write_dos_attributes
%C%_read_dos_attributes_SOURCES  = %D%/linux_dos_attributes/read_dos_attributes.c
%C%_write_dos_attributes_SOURCES = %D%/linux_dos_attributes/write_dos_attributes.c

scripts_zfs_tests_bin_PROGRAMS += %D%/randfree_file
%C%_randfree_file_SOURCES       = %D%/file/randfree_file.c

scripts_zfs_tests_bin_PROGRAMS += %D%/file_fadvise
%C%_file_fadvise_SOURCES  = %D%/file/file_fadvise.c
endif
