dnl #
dnl # 2.6.39 API change
dnl #
dnl # 33ee3b2e2eb9 kstrto*: converting strings to integers done (hopefully) right
dnl #
dnl # If kstrtoul() doesn't exist, fallback to use strict_strtoul() which has
dnl # existed since 2.6.25.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_KSTRTOUL], [
	AC_MSG_CHECKING([whether kstrtoul() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/kernel.h>
	],[
		int ret __attribute__ ((unused)) = kstrtoul(NULL, 10, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KSTRTOUL, 1, [kstrtoul() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
