dnl #
dnl # 4.17 API change
dnl # Added set_special_state() function
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SET_SPECIAL_STATE], [
	ZFS_LINUX_TEST_SRC([set_special_state], [
		#include <linux/sched.h>
	],[
		set_special_state(TASK_STOPPED);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SET_SPECIAL_STATE], [
	AC_MSG_CHECKING([whether set_special_state() exists])
	ZFS_LINUX_TEST_RESULT([set_special_state], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_SPECIAL_STATE, 1, [set_special_state() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
