dnl #
dnl # 3.6 API change,
dnl # 'sget' now takes one more argument.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_5ARG_SGET],
	[AC_MSG_CHECKING([whether sget() wants 5 args])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		(void) sget(NULL, NULL, NULL, 0, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_5ARG_SGET, 1, [sget() wants 5 args])
	],[
		AC_MSG_RESULT(no)
	])
])

