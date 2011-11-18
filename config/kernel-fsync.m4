dnl #
dnl # Linux 2.6.x - 2.6.34 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FSYNC_WITH_DENTRY], [
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*fsync) (struct file *, struct dentry *, int) = NULL;
		struct file_operations fops __attribute__ ((unused));

		fops.fsync = fsync;
	],[
		AC_MSG_RESULT([dentry])
		AC_DEFINE(HAVE_FSYNC_WITH_DENTRY, 1,
			[fops->fsync() with dentry])
	],[
	])
])

dnl #
dnl # Linux 2.6.35 - Linux 3.0 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FSYNC_WITHOUT_DENTRY], [
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*fsync) (struct file *, int) = NULL;
		struct file_operations fops __attribute__ ((unused));

		fops.fsync = fsync;
	],[
		AC_MSG_RESULT([no dentry])
		AC_DEFINE(HAVE_FSYNC_WITHOUT_DENTRY, 1,
			[fops->fsync() without dentry])
	],[
	])
])

dnl #
dnl # Linux 3.1 -x 3.x API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FSYNC_RANGE], [
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*fsync) (struct file *, loff_t, loff_t, int) = NULL;
		struct file_operations fops __attribute__ ((unused));

		fops.fsync = fsync;
	],[
		AC_MSG_RESULT([range])
		AC_DEFINE(HAVE_FSYNC_RANGE, 1,
			[fops->fsync() with range])
	],[
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FSYNC], [
	AC_MSG_CHECKING([whether fops->fsync() wants])
	ZFS_AC_KERNEL_FSYNC_WITH_DENTRY
	ZFS_AC_KERNEL_FSYNC_WITHOUT_DENTRY
	ZFS_AC_KERNEL_FSYNC_RANGE
])
