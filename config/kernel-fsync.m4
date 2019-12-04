dnl #
dnl # Check file_operations->fsync interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FSYNC], [
	ZFS_LINUX_TEST_SRC([fsync_without_dentry], [
		#include <linux/fs.h>

		int test_fsync(struct file *f, int x) { return 0; }

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.fsync = test_fsync,
		};
	],[])

	ZFS_LINUX_TEST_SRC([fsync_range], [
		#include <linux/fs.h>

		int test_fsync(struct file *f, loff_t a, loff_t b, int c)
		    { return 0; }

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.fsync = test_fsync,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_FSYNC], [
	dnl #
	dnl # Linux 2.6.35 - Linux 3.0 API
	dnl #
	AC_MSG_CHECKING([whether fops->fsync() wants no dentry])
	ZFS_LINUX_TEST_RESULT([fsync_without_dentry], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_FSYNC_WITHOUT_DENTRY, 1,
		    [fops->fsync() without dentry])
	],[
		AC_MSG_RESULT([no])

		dnl #
		dnl # Linux 3.1 - 3.x API
		dnl #
		AC_MSG_CHECKING([whether fops->fsync() wants range])
		ZFS_LINUX_TEST_RESULT([fsync_range], [
			AC_MSG_RESULT([range])
			AC_DEFINE(HAVE_FSYNC_RANGE, 1,
			    [fops->fsync() with range])
		],[
			ZFS_LINUX_TEST_ERROR([fops->fsync])
		])
	])
])
