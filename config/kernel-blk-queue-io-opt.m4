dnl #
dnl # 2.6.30 API change
dnl # The blk_queue_io_opt() function was added to indicate the optimal
dnl # I/O size for the device.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_IO_OPT], [
	AC_MSG_CHECKING([whether blk_queue_io_opt() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		unsigned int opt = 1;
		(void) blk_queue_io_opt(q, opt);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_IO_OPT, 1,
		          [blk_queue_io_opt() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
