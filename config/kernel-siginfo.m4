dnl #
dnl # 4.20 API change
dnl # Added kernel_siginfo_t
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SIGINFO], [
	ZFS_LINUX_TEST_SRC([siginfo], [
		#include <linux/signal_types.h>
	],[
		kernel_siginfo_t info __attribute__ ((unused));
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SIGINFO], [
	AC_MSG_CHECKING([whether kernel_siginfo_t tyepedef exists])
	ZFS_LINUX_TEST_RESULT([siginfo], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SIGINFO, 1, [kernel_siginfo_t exists])
	],[
		AC_MSG_RESULT(no)
	])
])
