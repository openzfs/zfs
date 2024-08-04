dnl #
dnl # 4.10 API change
dnl # has_capability() was exported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_HAS_CAPABILITY], [
	ZFS_LINUX_TEST_SRC([has_capability], [
		#include <linux/capability.h>
	],[
		struct task_struct *task = NULL;
		int cap = 0;
		bool result __attribute__ ((unused));

		result = has_capability(task, cap);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_HAS_CAPABILITY], [
	AC_MSG_CHECKING([whether has_capability() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([has_capability],
	    [has_capability], [kernel/capability.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_HAS_CAPABILITY, 1, [has_capability() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_USERNS_CAPABILITIES], [
	ZFS_AC_KERNEL_SRC_HAS_CAPABILITY
])

AC_DEFUN([ZFS_AC_KERNEL_USERNS_CAPABILITIES], [
	ZFS_AC_KERNEL_HAS_CAPABILITY
])
