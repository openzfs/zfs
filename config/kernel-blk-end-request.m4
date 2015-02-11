dnl #
dnl # 2.6.31 API change
dnl # In 2.6.29 kernels blk_end_request() was a GPL-only symbol, this was
dnl # changed in 2.6.31 so it may be used by non-GPL modules.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_END_REQUEST], [
	AC_MSG_CHECKING([whether blk_end_request() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
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

	AC_MSG_CHECKING([whether blk_end_request() is GPL-only])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
		#include <linux/blkdev.h>
		
		MODULE_LICENSE("$ZFS_META_LICENSE");
	],[
		struct request *req = NULL;
		(void) blk_end_request(req, 0, 0);
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_END_REQUEST_GPL_ONLY, 1,
		          [blk_end_request() is GPL-only])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
