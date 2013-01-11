dnl #
dnl # 2.6.31 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_RQ_SECTORS], [
	AC_MSG_CHECKING([whether blk_rq_sectors() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request *req = NULL;
		(void) blk_rq_sectors(req);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_RQ_SECTORS, 1,
		          [blk_rq_sectors() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
