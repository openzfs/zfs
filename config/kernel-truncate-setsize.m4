dnl #
dnl # 2.6.35 API change
dnl # Added truncate_setsize() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TRUNCATE_SETSIZE], [
	ZFS_LINUX_TEST_SRC([truncate_setsize], [
		#include <linux/mm.h>
	], [
		truncate_setsize(NULL, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_TRUNCATE_SETSIZE], [
	AC_MSG_CHECKING([whether truncate_setsize() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([truncate_setsize],
	    [truncate_setsize], [mm/truncate.c], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([truncate_setsize])
	])
])
