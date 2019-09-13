dnl #
dnl # 2.6.34 API change
dnl # blk_queue_max_hw_sectors() replaces blk_queue_max_sectors().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_HW_SECTORS], [
	ZFS_LINUX_TEST_SRC([blk_queue_max_hw_sectors], [
		#include <linux/blkdev.h>
	], [
		struct request_queue *q = NULL;
		(void) blk_queue_max_hw_sectors(q, BLK_SAFE_MAX_SECTORS);
	], [$NO_UNUSED_BUT_SET_VARIABLE])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS], [
	AC_MSG_CHECKING([whether blk_queue_max_hw_sectors() is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_max_hw_sectors], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_MAX_HW_SECTORS, 1,
		    [blk_queue_max_hw_sectors() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
