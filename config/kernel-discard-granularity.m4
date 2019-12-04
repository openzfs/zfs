dnl #
dnl # 2.6.33 API change
dnl # Discard granularity and alignment restrictions may now be set.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_DISCARD_GRANULARITY], [
	ZFS_LINUX_TEST_SRC([discard_granularity], [
		#include <linux/blkdev.h>
	],[
		struct queue_limits ql __attribute__ ((unused));
		ql.discard_granularity = 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_DISCARD_GRANULARITY], [
	AC_MSG_CHECKING([whether ql->discard_granularity is available])
	ZFS_LINUX_TEST_RESULT([discard_granularity], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([ql->discard_granularity])
	])
])
