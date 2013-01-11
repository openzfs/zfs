dnl #
dnl # 2.6.31 API change
dnl # Request queue peek/retrieval interface cleanup, the
dnl # elv_requeue_request() function has been replaced with the
dnl # blk_requeue_request() function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_REQUEUE_REQUEST], [
	AC_MSG_CHECKING([whether blk_requeue_request() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		struct request *req = NULL;
		blk_requeue_request(q, req);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_REQUEUE_REQUEST, 1,
		          [blk_requeue_request() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
