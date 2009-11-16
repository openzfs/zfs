dnl #
dnl # 2.6.18 API change
nl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_END_REQUEST], [
	AC_MSG_CHECKING([whether blk_end_request() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request *req = NULL;
		(void) blk_end_request(req, 0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_END_REQUEST, 1,
		          [blk_end_request() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
