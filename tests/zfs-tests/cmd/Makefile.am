include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin


pkgexec_PROGRAMS = badsend
badsend_SOURCES = badsend.c
badsend_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la


pkgexec_PROGRAMS += btree_test
btree_test_SOURCES = btree_test.c
# Unconditionally enable ASSERTs
btree_test_CPPFLAGS = $(AM_CPPFLAGS) -DDEBUG -UNDEBUG -DZFS_DEBUG
btree_test_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la


pkgexec_PROGRAMS += chg_usr_exec
chg_usr_exec_SOURCES = chg_usr_exec.c


if WANT_DEVNAME2DEVID
pkgexec_PROGRAMS += devname2devid
devname2devid_SOURCES = devname2devid.c
devname2devid_CFLAGS = $(AM_CFLAGS) $(LIBUDEV_CFLAGS)
devname2devid_LDADD = $(LIBUDEV_LIBS)
endif


pkgexec_PROGRAMS += dir_rd_update
dir_rd_update_SOURCES = dir_rd_update.c


pkgexec_PROGRAMS += draid
draid_CFLAGS = $(AM_CFLAGS) $(ZLIB_CFLAGS)
draid_SOURCES = draid.c
draid_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la
draid_LDADD += $(ZLIB_LIBS)


EXTRA_DIST = file/file_common.h
pkgexec_PROGRAMS += file_check file_trunc file_write largest_file randwritecomp
file_check_SOURCES = file/file_check.c
file_trunc_SOURCES = file/file_trunc.c
file_write_SOURCES = file/file_write.c
largest_file_SOURCES = file/largest_file.c
randwritecomp_SOURCES = file/randwritecomp.c


pkgexec_PROGRAMS += get_diff
get_diff_SOURCES = get_diff.c


pkgexec_PROGRAMS += libzfs_input_check
libzfs_input_check_SOURCES = libzfs_input_check.c
if BUILD_FREEBSD
libzfs_input_check_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include/os/freebsd/zfs
endif
if BUILD_LINUX
libzfs_input_check_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/include/os/linux/zfs
endif
libzfs_input_check_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la



pkgexec_PROGRAMS += mkbusy mkfile mkfiles mktree
mkbusy_SOURCES = mkbusy.c
mkfile_SOURCES = mkfile.c
mkfile_LDADD = $(LTLIBINTL)
mkfiles_SOURCES = mkfiles.c
mktree_SOURCES = mktree.c


pkgexec_PROGRAMS += mmap_exec mmap_seek mmapwrite readmmap
mmap_exec_SOURCES = mmap_exec.c
mmap_seek_SOURCES = mmap_seek.c
mmapwrite_SOURCES = mmapwrite.c
mmapwrite_LDADD = -lpthread
readmmap_SOURCES = readmmap.c

if WANT_MMAP_LIBAIO
pkgexec_PROGRAMS += mmap_libaio
mmap_libaio_SOURCES = mmap_libaio.c
mmap_libaio_CFLAGS = $(AM_CFLAGS) $(LIBAIO_CFLAGS)
mmap_libaio_LDADD = $(LIBAIO_LIBS)
endif


pkgexec_PROGRAMS += nvlist_to_lua
nvlist_to_lua_SOURCES = nvlist_to_lua.c
nvlist_to_lua_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la


pkgexec_PROGRAMS += rename_dir
rename_dir_SOURCES = rename_dir.c

pkgexec_PROGRAMS += rm_lnkcnt_zero_file
rm_lnkcnt_zero_file_SOURCES = rm_lnkcnt_zero_file.c
rm_lnkcnt_zero_file_LDADD = -lpthread

pkgexec_PROGRAMS += send_doall
send_doall_SOURCES = send_doall.c
send_doall_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

pkgexec_PROGRAMS += stride_dd
stride_dd_SOURCES = stride_dd.c
stride_dd_LDADD = -lrt

pkgexec_PROGRAMS += threadsappend
threadsappend_SOURCES = threadsappend.c
threadsappend_LDADD = -lpthread


if BUILD_LINUX
pkgexec_PROGRAMS += getversion
getversion_SOURCES = getversion.c

EXTRA_DIST += linux_dos_attributes/dos_attributes.h
pkgexec_PROGRAMS += read_dos_attributes write_dos_attributes
read_dos_attributes_SOURCES = linux_dos_attributes/read_dos_attributes.c
write_dos_attributes_SOURCES = linux_dos_attributes/write_dos_attributes.c

pkgexec_PROGRAMS += randfree_file
randfree_file_SOURCES = file/randfree_file.c

pkgexec_PROGRAMS += user_ns_exec
user_ns_exec_SOURCES = user_ns_exec.c

pkgexec_PROGRAMS += xattrtest
xattrtest_SOURCES = xattrtest.c
endif
