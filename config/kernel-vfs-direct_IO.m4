dnl #
dnl # Linux 4.6.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO_ITER], [
	AC_MSG_CHECKING([whether aops->direct_IO() uses iov_iter])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_direct_IO(struct kiocb *kiocb,
		    struct iov_iter *iter) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.direct_IO = test_direct_IO,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER, 1,
		    [aops->direct_IO() uses iov_iter without rw])
		zfs_ac_direct_io="yes"
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Linux 4.1.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO_ITER_OFFSET], [
	AC_MSG_CHECKING(
	    [whether aops->direct_IO() uses iov_iter with offset])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_direct_IO(struct kiocb *kiocb,
		    struct iov_iter *iter, loff_t offset) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.direct_IO = test_direct_IO,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER_OFFSET, 1,
		    [aops->direct_IO() uses iov_iter with offset])
		zfs_ac_direct_io="yes"
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Linux 3.16.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO_ITER_RW_OFFSET], [
	AC_MSG_CHECKING(
	    [whether aops->direct_IO() uses iov_iter with rw and offset])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_direct_IO(int rw, struct kiocb *kiocb,
		    struct iov_iter *iter, loff_t offset) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
		    .direct_IO = test_direct_IO,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER_RW_OFFSET, 1,
		    [aops->direct_IO() uses iov_iter with rw and offset])
		zfs_ac_direct_io="yes"
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Ancient Linux API (predates git)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO_IOVEC], [
	AC_MSG_CHECKING([whether aops->direct_IO() uses iovec])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_direct_IO(int rw, struct kiocb *kiocb,
		    const struct iovec *iov, loff_t offset,
		    unsigned long nr_segs) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
		    .direct_IO = test_direct_IO,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_DIRECT_IO_IOVEC, 1,
		    [aops->direct_IO() uses iovec])
		zfs_ac_direct_io="yes"
	],[
		AC_MSG_RESULT([no])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO], [
	zfs_ac_direct_io="no"

	if test "$zfs_ac_direct_io" = "no"; then
		ZFS_AC_KERNEL_VFS_DIRECT_IO_ITER
	fi

	if test "$zfs_ac_direct_io" = "no"; then
		ZFS_AC_KERNEL_VFS_DIRECT_IO_ITER_OFFSET
	fi

	if test "$zfs_ac_direct_io" = "no"; then
		ZFS_AC_KERNEL_VFS_DIRECT_IO_ITER_RW_OFFSET
	fi

	if test "$zfs_ac_direct_io" = "no"; then
		ZFS_AC_KERNEL_VFS_DIRECT_IO_IOVEC
	fi

	if test "$zfs_ac_direct_io" = "no"; then
		AC_MSG_ERROR([no; unknown direct IO interface])
	fi
])
