dnl #
dnl # 2.6.30 API change
dnl # The blk_queue_physical_block_size() function was introduced to
dnl # indicate the smallest I/O the device can write without incurring
dnl # a read-modify-write penalty.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_PHYSICAL_BLOCK_SIZE], [
	AC_MSG_CHECKING([whether blk_queue_physical_block_size() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		unsigned short block_size = 1;
		(void) blk_queue_physical_block_size(q, block_size);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_PHYSICAL_BLOCK_SIZE, 1,
		          [blk_queue_physical_block_size() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
