dnl #
dnl # 2.6.36 API compatibility- Added usleep_range timer.
dnl #
dnl # usleep_range is a finer precision implementation of msleep
dnl # designed to be a drop-in replacement for udelay where a precise
dnl # sleep / busy-wait is unnecessary.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_USLEEP_RANGE], [
	ZFS_LINUX_TEST_SRC([usleep_range], [
		#include <linux/delay.h>
	],[
		usleep_range(0, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_USLEEP_RANGE], [
	AC_MSG_CHECKING([whether usleep_range() is available])
	ZFS_LINUX_TEST_RESULT([usleep_range], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([usleep_range()])
	])
])
