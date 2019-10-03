dnl #
dnl # 2.6.32 - 4.11, statically allocated bdi in request_queue
dnl # 4.12 - x.y, dynamically allocated bdi in request_queue
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_BDI], [
	ZFS_LINUX_TEST_SRC([blk_queue_bdi], [
		#include <linux/blkdev.h>
	],[
		struct request_queue q;
		struct backing_dev_info bdi;
		q.backing_dev_info = &bdi;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_BDI], [
	AC_MSG_CHECKING([whether blk_queue bdi is dynamic])
	ZFS_LINUX_TEST_RESULT([blk_queue_bdi], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_BDI_DYNAMIC, 1,
		    [blk queue backing_dev_info is dynamic])
	],[
		AC_MSG_RESULT(no)
	])
])
