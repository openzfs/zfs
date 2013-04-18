dnl #
dnl # 2.6.27 API change
dnl # The blk_queue_nonrot() function and QUEUE_FLAG_NONROT flag were
dnl # added so non-rotational devices could be identified.  These devices
dnl # have no seek time which the higher level elevator uses to optimize
dnl # how the I/O issued to the device.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_NONROT], [
	AC_MSG_CHECKING([whether blk_queue_nonrot() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		(void) blk_queue_nonrot(q);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_NONROT, 1,
		          [blk_queue_nonrot() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
