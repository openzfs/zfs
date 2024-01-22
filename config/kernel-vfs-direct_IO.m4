dnl #
dnl # Check for direct IO interfaces.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_DIRECT_IO], [
	ZFS_LINUX_TEST_SRC([direct_io_iter], [
		#include <linux/fs.h>

		static ssize_t test_direct_IO(struct kiocb *kiocb,
		    struct iov_iter *iter) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.direct_IO = test_direct_IO,
		};
	],[])

	ZFS_LINUX_TEST_SRC([direct_io_iter_offset], [
		#include <linux/fs.h>

		static ssize_t test_direct_IO(struct kiocb *kiocb,
		    struct iov_iter *iter, loff_t offset) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.direct_IO = test_direct_IO,
		};
	],[])

	ZFS_LINUX_TEST_SRC([direct_io_iter_rw_offset], [
		#include <linux/fs.h>

		static ssize_t test_direct_IO(int rw, struct kiocb *kiocb,
		    struct iov_iter *iter, loff_t offset) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
		    .direct_IO = test_direct_IO,
		};
	],[])

	ZFS_LINUX_TEST_SRC([direct_io_iovec], [
		#include <linux/fs.h>

		static ssize_t test_direct_IO(int rw, struct kiocb *kiocb,
		    const struct iovec *iov, loff_t offset,
		    unsigned long nr_segs) { return 0; }

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
		    .direct_IO = test_direct_IO,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO], [
	dnl #
	dnl # Linux 4.6.x API change
	dnl #
	AC_MSG_CHECKING([whether aops->direct_IO() uses iov_iter])
	ZFS_LINUX_TEST_RESULT([direct_io_iter], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER, 1,
		    [aops->direct_IO() uses iov_iter without rw])
	],[
		AC_MSG_RESULT([no])

		dnl #
		dnl # Linux 4.1.x API change
		dnl #
		AC_MSG_CHECKING(
		    [whether aops->direct_IO() uses offset])
		ZFS_LINUX_TEST_RESULT([direct_io_iter_offset], [
			AC_MSG_RESULT([yes])
			AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER_OFFSET, 1,
			    [aops->direct_IO() uses iov_iter with offset])

		],[
			AC_MSG_RESULT([no])

			dnl #
			dnl # Linux 3.16.x API change
			dnl #
			AC_MSG_CHECKING(
			    [whether aops->direct_IO() uses rw and offset])
			ZFS_LINUX_TEST_RESULT([direct_io_iter_rw_offset], [
				AC_MSG_RESULT([yes])
				AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER_RW_OFFSET, 1,
				    [aops->direct_IO() uses iov_iter with ]
				    [rw and offset])
			],[
				AC_MSG_RESULT([no])

				dnl #
				dnl # Ancient Linux API (predates git)
				dnl #
				AC_MSG_CHECKING(
				    [whether aops->direct_IO() uses iovec])
				ZFS_LINUX_TEST_RESULT([direct_io_iovec], [
					AC_MSG_RESULT([yes])
					AC_DEFINE(HAVE_VFS_DIRECT_IO_IOVEC, 1,
					    [aops->direct_IO() uses iovec])
				],[
					ZFS_LINUX_TEST_ERROR([direct IO])
					AC_MSG_RESULT([no])
				])
			])
		])
	])
])
