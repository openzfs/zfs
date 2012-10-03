dnl #
dnl # 2.6.32 API change
dnl # max_discard_sectors is available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MAX_DISCARD_SECTORS], [
	AC_MSG_CHECKING([whether ql->max_discard_sectors is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct queue_limits ql __attribute__ ((unused));

		ql.max_discard_sectors = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MAX_DISCARD_SECTORS, 1,
		          [ql->max_discard_sectors is available])
	],[
		AC_MSG_RESULT(no)
	])
])
