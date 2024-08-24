dnl #
dnl # 3.9 API change,
dnl # Moved things from linux/sched.h to linux/sched/rt.h
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SCHED_RT_HEADER], [
	ZFS_LINUX_TEST_SRC([sched_rt_header], [
		#include <linux/sched.h>
		#include <linux/sched/rt.h>
	],[
		return 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SCHED_RT_HEADER], [
	AC_MSG_CHECKING([whether header linux/sched/rt.h exists])
	ZFS_LINUX_TEST_RESULT([sched_rt_header], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([sched_rt_header])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SCHED], [
	ZFS_AC_KERNEL_SRC_SCHED_RT_HEADER
])

AC_DEFUN([ZFS_AC_KERNEL_SCHED], [
	ZFS_AC_KERNEL_SCHED_RT_HEADER
])
