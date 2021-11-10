dnl #
dnl # 4.9, current_time() added
dnl # 4.18, return type changed from timespec to timespec64
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CURRENT_TIME], [
	ZFS_LINUX_TEST_SRC([current_time], [
		#include <linux/fs.h>
	], [
		struct inode ip __attribute__ ((unused));
		ip.i_atime = current_time(&ip);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CURRENT_TIME], [
	AC_MSG_CHECKING([whether current_time() exists])
	ZFS_LINUX_TEST_RESULT_SYMBOL([current_time],
	    [current_time], [fs/inode.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CURRENT_TIME, 1, [current_time() exists])
	], [
		AC_MSG_RESULT(no)
	])
])
