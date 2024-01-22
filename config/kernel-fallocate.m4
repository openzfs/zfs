dnl #
dnl # Linux 2.6.38 - 3.x API
dnl # The fallocate callback was moved from the inode_operations
dnl # structure to the file_operations structure.
dnl #
dnl #
dnl # Linux 3.15+
dnl # fallocate learned a new flag, FALLOC_FL_ZERO_RANGE
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FALLOCATE], [
	ZFS_LINUX_TEST_SRC([file_fallocate], [
		#include <linux/fs.h>

		static long test_fallocate(struct file *file, int mode,
		    loff_t offset, loff_t len) { return 0; }

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.fallocate = test_fallocate,
		};
	], [])
	ZFS_LINUX_TEST_SRC([falloc_fl_zero_range], [
		#include <linux/falloc.h>
	],[
		int flags __attribute__ ((unused));
		flags = FALLOC_FL_ZERO_RANGE;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FALLOCATE], [
	AC_MSG_CHECKING([whether fops->fallocate() exists])
	ZFS_LINUX_TEST_RESULT([file_fallocate], [
		AC_MSG_RESULT(yes)
		AC_MSG_CHECKING([whether FALLOC_FL_ZERO_RANGE exists])
		ZFS_LINUX_TEST_RESULT([falloc_fl_zero_range], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_FALLOC_FL_ZERO_RANGE, 1, [FALLOC_FL_ZERO_RANGE is defined])
		],[
			AC_MSG_RESULT(no)
		])
	],[
		ZFS_LINUX_TEST_ERROR([file_fallocate])
	])
])
