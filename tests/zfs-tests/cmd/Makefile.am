# SPDX-License-Identifier: CDDL-1.0
scripts_zfs_tests_bindir = $(datadir)/$(PACKAGE)/zfs-tests/bin

# Default for helpers that do not set _LDADD. --enable-debug turns VERIFY()
# into libspl_assertf, which lives in the convenience lib libspl_assert.la
# (libspl.la does not pull that in — both are noinst). Directory-prefixed
# names (%D%/foo) do not always inherit LDADD on automake 1.16, so every
# helper without a private _LDADD is listed below.
LDADD = libspl_assert.la libspl.la
%C%_chg_usr_exec_LDADD = libspl_assert.la libspl.la
%C%_clonefile_LDADD = libspl_assert.la libspl.la
%C%_clone_mmap_cached_LDADD = libspl_assert.la libspl.la
%C%_clone_mmap_write_LDADD = libspl_assert.la libspl.la
%C%_cp_files_LDADD = libspl_assert.la libspl.la
%C%_ctime_LDADD = libspl_assert.la libspl.la
%C%_dir_rd_update_LDADD = libspl_assert.la libspl.la
%C%_dosmode_readonly_write_LDADD = libspl_assert.la libspl.la
%C%_get_diff_LDADD = libspl_assert.la libspl.la
%C%_rename_dir_LDADD = libspl_assert.la libspl.la
%C%_suid_write_to_file_LDADD = libspl_assert.la libspl.la
%C%_truncate_test_LDADD = libspl_assert.la libspl.la
%C%_zfs_diff_socket_LDADD = libspl_assert.la libspl.la
%C%_clone_after_trunc_LDADD = libspl_assert.la libspl.la
%C%_file_append_LDADD = libspl_assert.la libspl.la
%C%_file_check_LDADD = libspl_assert.la libspl.la
%C%_file_trunc_LDADD = libspl_assert.la libspl.la
%C%_file_write_LDADD = libspl_assert.la libspl.la
%C%_largest_file_LDADD = libspl_assert.la libspl.la
%C%_randfree_file_LDADD = libspl_assert.la libspl.la
%C%_randwritecomp_LDADD = libspl_assert.la libspl.la
%C%_manipulate_user_buffer_LDADD = libspl_assert.la libspl.la
%C%_mkbusy_LDADD = libspl_assert.la libspl.la
%C%_mkfiles_LDADD = libspl_assert.la libspl.la
%C%_mktree_LDADD = libspl_assert.la libspl.la
%C%_mmap_exec_LDADD = libspl_assert.la libspl.la
%C%_mmap_ftruncate_LDADD = libspl_assert.la libspl.la
%C%_mmap_seek_LDADD = libspl_assert.la libspl.la
%C%_mmap_sync_LDADD = libspl_assert.la libspl.la
%C%_mmapwrite_LDADD = libspl_assert.la libspl.la
%C%_readmmap_LDADD = libspl_assert.la libspl.la
%C%_mmap_write_sync_LDADD = libspl_assert.la libspl.la
%C%_mmap_read_truncate_LDADD = libspl_assert.la libspl.la
%C%_rm_lnkcnt_zero_file_LDADD = libspl_assert.la libspl.la
%C%_threadsappend_LDADD = libspl_assert.la libspl.la
%C%_file_fadvise_LDADD = libspl_assert.la libspl.la
%C%_file_unlink_fh_LDADD = libspl_assert.la libspl.la

scripts_zfs_tests_bin_PROGRAMS  = %D%/chg_usr_exec
scripts_zfs_tests_bin_PROGRAMS += %D%/clonefile
scripts_zfs_tests_bin_PROGRAMS += %D%/clone_mmap_cached
scripts_zfs_tests_bin_PROGRAMS += %D%/clone_mmap_write
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
%C%_btree_test_CPPFLAGS = $(AM_CPPFLAGS) $(LIBZPOOL_CPPFLAGS)
%C%_btree_test_LDADD = \
	libzpool.la \
	libbtree.la

scripts_zfs_tests_bin_PROGRAMS += %D%/crypto_test
%C%_crypto_test_SOURCES = %D%/crypto_test.c
%C%_crypto_test_LDADD = libzpool.la

scripts_zfs_tests_bin_PROGRAMS += %D%/clone_after_trunc

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
scripts_zfs_tests_bin_PROGRAMS += %D%/file_append %D%/file_check %D%/file_trunc %D%/file_write %D%/largest_file %D%/randfree_file %D%/randwritecomp
%C%_file_append_SOURCES   = %D%/file/file_append.c
%C%_file_check_SOURCES    = %D%/file/file_check.c
%C%_file_trunc_SOURCES    = %D%/file/file_trunc.c
%C%_file_write_SOURCES    = %D%/file/file_write.c
%C%_largest_file_SOURCES  = %D%/file/largest_file.c
%C%_randfree_file_SOURCES = %D%/file/randfree_file.c
%C%_randwritecomp_SOURCES = %D%/file/randwritecomp.c


scripts_zfs_tests_bin_PROGRAMS += %D%/libzfs_input_check
%C%_libzfs_input_check_LDADD = \
	libzfs_core.la \
	libnvpair.la

scripts_zfs_tests_bin_PROGRAMS += %D%/libzfs_mnttab_cache_check
%C%_libzfs_mnttab_cache_check_LDADD = \
	libzfs.la

scripts_zfs_tests_bin_PROGRAMS += %D%/manipulate_user_buffer

scripts_zfs_tests_bin_PROGRAMS += %D%/mkbusy %D%/mkfile %D%/mkfiles %D%/mktree
%C%_mkfile_LDADD = $(LTLIBINTL) libspl_assert.la libspl.la
if BUILD_NETBSD
# --disable-nls leaves LTLIBINTL empty, but mkfile.c still calls gettext().
%C%_mkfile_LDADD += -lintl
endif


scripts_zfs_tests_bin_PROGRAMS += \
	%D%/mmap_exec %D%/mmap_ftruncate %D%/mmap_seek \
	%D%/mmap_sync %D%/mmapwrite %D%/readmmap %D%/mmap_write_sync \
	%D%/mmap_read_truncate

if WANT_MMAP_LIBAIO
scripts_zfs_tests_bin_PROGRAMS += %D%/mmap_libaio
%C%_mmap_libaio_CFLAGS = $(AM_CFLAGS) $(LIBAIO_CFLAGS)
%C%_mmap_libaio_LDADD  = $(LIBAIO_LIBS) libspl_assert.la libspl.la
endif

scripts_zfs_tests_bin_PROGRAMS += %D%/zcp_support
%C%_zcp_support_LDADD = \
	libzfs.la \
	libnvpair.la

%C%_zcp_support_SOURCES = %D%/zcp_support.c
%C%_zcp_support_SOURCES_DIST = %D%/zcp_support.c

scripts_zfs_tests_bin_PROGRAMS += %D%/nvlist_to_lua
%C%_nvlist_to_lua_LDADD = \
	libzfs_core.la \
	libnvpair.la

%C%_nvlist_to_lua_SOURCES = %D%/nvlist_to_lua.c
%C%_nvlist_to_lua_SOURCES_DIST = %D%/nvlist_to_lua.c

scripts_zfs_tests_bin_PROGRAMS += %D%/rm_lnkcnt_zero_file

scripts_zfs_tests_bin_PROGRAMS += %D%/send_doall
%C%_send_doall_LDADD = \
	libzfs_core.la \
	libzfs.la \
	libnvpair.la

scripts_zfs_tests_bin_PROGRAMS += %D%/stride_dd
%C%_stride_dd_LDADD = -lrt libspl_assert.la libspl.la

scripts_zfs_tests_bin_PROGRAMS += %D%/threadsappend

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
	libzpool.la
%C%_sha2_test_LDADD  = $(%C%_skein_test_LDADD)
%C%_edonr_test_LDADD = $(%C%_skein_test_LDADD)
%C%_blake3_test_LDADD = $(%C%_skein_test_LDADD)

if BUILD_LINUX
scripts_zfs_tests_bin_PROGRAMS += %D%/getversion
scripts_zfs_tests_bin_PROGRAMS += %D%/user_ns_exec
scripts_zfs_tests_bin_PROGRAMS += %D%/makedir
scripts_zfs_tests_bin_PROGRAMS += %D%/mount_null_source
scripts_zfs_tests_bin_PROGRAMS += %D%/renameat2
scripts_zfs_tests_bin_PROGRAMS += %D%/statx
scripts_zfs_tests_bin_PROGRAMS += %D%/xattrtest
scripts_zfs_tests_bin_PROGRAMS += %D%/zed_fd_spill-zedlet
scripts_zfs_tests_bin_PROGRAMS += %D%/idmap_util
scripts_zfs_tests_bin_PROGRAMS += %D%/setlease

%C%_idmap_util_LDADD = libspl_assert.la libspl.la

dist_noinst_DATA += %D%/linux_dos_attributes/dos_attributes.h
scripts_zfs_tests_bin_PROGRAMS  += %D%/read_dos_attributes %D%/write_dos_attributes
%C%_read_dos_attributes_SOURCES  = %D%/linux_dos_attributes/read_dos_attributes.c
%C%_write_dos_attributes_SOURCES = %D%/linux_dos_attributes/write_dos_attributes.c
endif

scripts_zfs_tests_bin_PROGRAMS += %D%/file_fadvise
%C%_file_fadvise_SOURCES  = %D%/file/file_fadvise.c

scripts_zfs_tests_bin_PROGRAMS += %D%/file_uncached
%C%_file_uncached_SOURCES  = %D%/file/file_uncached.c

scripts_zfs_tests_bin_PROGRAMS += %D%/file_unlink_fh
%C%_file_unlink_fh_SOURCES  = %D%/file/file_unlink_fh.c

if BUILD_NETBSD
scripts_zfs_tests_bin_PROGRAMS += %D%/xattrtest
scripts_zfs_tests_bin_PROGRAMS += %D%/fioseek
%C%_fioseek_SOURCES = %D%/fioseek.c
%C%_fioseek_LDADD = libspl_assert.la libspl.la
endif
