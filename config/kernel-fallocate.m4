dnl #
dnl # Linux 2.6.38 - 3.x API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FILE_FALLOCATE], [
	AC_MSG_CHECKING([whether fops->fallocate() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		long (*fallocate) (struct file *, int, loff_t, loff_t) = NULL;
		struct file_operations fops __attribute__ ((unused)) = {
			.fallocate = fallocate,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_FALLOCATE, 1, [fops->fallocate() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 2.6.x - 2.6.37 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_FALLOCATE], [
	AC_MSG_CHECKING([whether iops->fallocate() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		long (*fallocate) (struct inode *, int, loff_t, loff_t) = NULL;
		struct inode_operations fops __attribute__ ((unused)) = {
			.fallocate = fallocate,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_FALLOCATE, 1, [fops->fallocate() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # The fallocate callback was moved from the inode_operations
dnl # structure to the file_operations structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FALLOCATE], [
	ZFS_AC_KERNEL_FILE_FALLOCATE
	ZFS_AC_KERNEL_INODE_FALLOCATE
])
