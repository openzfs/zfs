dnl #
dnl # 2.6.30 API change
dnl # bdev_hardsect_size() replaced with bdev_logical_block_size().  While
dnl # it has been true for a while that there was no strict 1:1 mapping
dnl # between physical sector size and logical block size this change makes
dnl # it explicit.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BDEV_LOGICAL_BLOCK_SIZE], [
	ZFS_LINUX_TEST_SRC([bdev_logical_block_size], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		bdev_logical_block_size(bdev);
	], [$NO_UNUSED_BUT_SET_VARIABLE])
])

AC_DEFUN([ZFS_AC_KERNEL_BDEV_LOGICAL_BLOCK_SIZE], [
	AC_MSG_CHECKING([whether bdev_logical_block_size() is available])
	ZFS_LINUX_TEST_RESULT([bdev_logical_block_size], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_LOGICAL_BLOCK_SIZE, 1,
		    [bdev_logical_block_size() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
