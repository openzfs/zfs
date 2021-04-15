dnl #
dnl # 4.4 API change
dnl # Added kernel_signal_stop
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SIGNAL_STOP], [
	ZFS_LINUX_TEST_SRC([signal_stop], [
		#include <linux/sched/signal.h>
	],[
		kernel_signal_stop();
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SIGNAL_STOP], [
	AC_MSG_CHECKING([whether signal_stop() exists])
	ZFS_LINUX_TEST_RESULT([signal_stop], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SIGNAL_STOP, 1, [signal_stop() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
