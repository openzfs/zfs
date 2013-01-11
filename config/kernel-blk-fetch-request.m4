dnl #
dnl # 2.6.31 API change
dnl # Request queue peek/retrieval interface cleanup, the blk_fetch_request()
dnl # function replaces the elv_next_request() and blk_fetch_request()
dnl # functions.  The updated blk_fetch_request() function returns the
dnl # next available request and removed it from the request queue.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_FETCH_REQUEST], [
	AC_MSG_CHECKING([whether blk_fetch_request() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		(void) blk_fetch_request(q);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_FETCH_REQUEST, 1,
		          [blk_fetch_request() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
