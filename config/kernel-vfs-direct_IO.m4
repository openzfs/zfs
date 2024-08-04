dnl #
dnl # Check for Direct I/O interfaces.
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
			ZFS_LINUX_TEST_ERROR([Direct I/O])
		])
	])
])
