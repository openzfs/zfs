dnl #
dnl # SB_DYING exists since Linux 6.6
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SB_DYING], [
	ZFS_LINUX_TEST_SRC([sb_dying], [
		#include <linux/fs.h>
	],[
		(void) SB_DYING;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SB_DYING], [
	AC_MSG_CHECKING([whether SB_DYING is defined])
	ZFS_LINUX_TEST_RESULT([sb_dying], [
		AC_MSG_RESULT(yes)
	],[
		AC_MSG_RESULT(no)
	])
])
