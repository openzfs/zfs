dnl #
dnl # 2.6.29 API change
dnl # In the 2.6.29 kernel blk_rq_bytes() was available as a GPL-only symbol.
dnl # So we need to check the symbol license as well.  As of 2.6.31 the
dnl blk_rq_bytes() helper was changed to a static inline which we can use.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_RQ_BYTES], [
	AC_MSG_CHECKING([whether blk_rq_bytes() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
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

	AC_MSG_CHECKING([whether blk_rq_bytes() is GPL-only])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
		#include <linux/blkdev.h>

		MODULE_LICENSE("$ZFS_META_LICENSE");
	],[
		struct request *req = NULL;
		(void) blk_rq_bytes(req);
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_RQ_BYTES_GPL_ONLY, 1,
		          [blk_rq_bytes() is GPL-only])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
