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

dnl #
dnl # 4.11 API change,
dnl # Moved things from linux/sched.h to linux/sched/signal.h
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SCHED_SIGNAL_HEADER], [
	ZFS_LINUX_TEST_SRC([sched_signal_header], [
		#include <linux/sched.h>
		#include <linux/sched/signal.h>
	],[
		return 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SCHED_SIGNAL_HEADER], [
	AC_MSG_CHECKING([whether header linux/sched/signal.h exists])
	ZFS_LINUX_TEST_RESULT([sched_signal_header], [
		AC_DEFINE(HAVE_SCHED_SIGNAL_HEADER, 1,
		    [linux/sched/signal.h exists])
		AC_MSG_RESULT(yes)
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.19 API change
dnl # The io_schedule_timeout() function is present in all 2.6.32 kernels
dnl # but it was not exported until Linux 3.19.  The RHEL 7.x kernels which
dnl # are based on a 3.10 kernel do export this symbol.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_IO_SCHEDULE_TIMEOUT], [
	ZFS_LINUX_TEST_SRC([io_schedule_timeout], [
		#include <linux/sched.h>
	], [
		(void) io_schedule_timeout(1);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_IO_SCHEDULE_TIMEOUT], [
	AC_MSG_CHECKING([whether io_schedule_timeout() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([io_schedule_timeout],
	[io_schedule_timeout], [], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IO_SCHEDULE_TIMEOUT, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SCHED], [
	ZFS_AC_KERNEL_SRC_SCHED_RT_HEADER
	ZFS_AC_KERNEL_SRC_SCHED_SIGNAL_HEADER
	ZFS_AC_KERNEL_SRC_IO_SCHEDULE_TIMEOUT
])

AC_DEFUN([ZFS_AC_KERNEL_SCHED], [
	ZFS_AC_KERNEL_SCHED_RT_HEADER
	ZFS_AC_KERNEL_SCHED_SIGNAL_HEADER
	ZFS_AC_KERNEL_IO_SCHEDULE_TIMEOUT
])
