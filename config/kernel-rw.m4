dnl #
dnl # 4.14 API change
dnl # kernel_write() which was introduced in 3.9 was updated to take
dnl # the offset as a pointer which is needed by vn_rdwr().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITE], [
	ZFS_LINUX_TEST_SRC([kernel_write], [
		#include <linux/fs.h>
	],[
		struct file *file = NULL;
		const void *buf = NULL;
		size_t count = 0;
		loff_t *pos = NULL;
		ssize_t ret;

		ret = kernel_write(file, buf, count, pos);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_WRITE], [
	AC_MSG_CHECKING([whether kernel_write() takes loff_t pointer])
	ZFS_LINUX_TEST_RESULT([kernel_write], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_WRITE_PPOS, 1,
		    [kernel_write() take loff_t pointer])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.14 API change
dnl # kernel_read() which has existed for forever was updated to take
dnl # the offset as a pointer which is needed by vn_rdwr().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_READ], [
	ZFS_LINUX_TEST_SRC([kernel_read], [
		#include <linux/fs.h>
	],[
		struct file *file = NULL;
		void *buf = NULL;
		size_t count = 0;
		loff_t *pos = NULL;
		ssize_t ret;

		ret = kernel_read(file, buf, count, pos);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_READ], [
	AC_MSG_CHECKING([whether kernel_read() takes loff_t pointer])
	ZFS_LINUX_TEST_RESULT([kernel_read], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_READ_PPOS, 1,
		    [kernel_read() take loff_t pointer])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_RW], [
	ZFS_AC_KERNEL_SRC_WRITE
	ZFS_AC_KERNEL_SRC_READ
])

AC_DEFUN([ZFS_AC_KERNEL_RW], [
	ZFS_AC_KERNEL_WRITE
	ZFS_AC_KERNEL_READ
])
