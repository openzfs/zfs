dnl #
dnl # 2.6.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_RQ_IS_SYNC], [
	AC_MSG_CHECKING([whether rq_is_sync() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request *req = NULL;
		(void) rq_is_sync(req);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RQ_IS_SYNC, 1,
		          [rq_is_sync() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
