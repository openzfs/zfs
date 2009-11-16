dnl #
dnl # 2.6.31 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_RQ_BYTES], [
	AC_MSG_CHECKING([whether blk_rq_bytes() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request *req = NULL;
		(void) blk_rq_bytes(req);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_RQ_BYTES, 1,
		          [blk_rq_bytes() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
