dnl #
dnl # 2.6.22 API change
dnl # Unused destroy_dirty_buffers arg removed from prototype.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INVALIDATE_BDEV], [
	ZFS_LINUX_TEST_SRC([invalidate_bdev], [
		#include <linux/buffer_head.h>
	],[
		struct block_device *bdev = NULL;
		invalidate_bdev(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INVALIDATE_BDEV], [
	AC_MSG_CHECKING([whether invalidate_bdev() wants 1 arg])
	ZFS_LINUX_TEST_RESULT([invalidate_bdev], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_INVALIDATE_BDEV, 1,
		    [invalidate_bdev() wants 1 arg])
	],[
		AC_MSG_RESULT(no)
	])
])
