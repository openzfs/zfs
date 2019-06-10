dnl #
dnl # API change
dnl # https://github.com/torvalds/linux/commit/8814ce8
dnl # Introduction of blk_queue_flag_set and blk_queue_flag_clear
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_SET], [
	ZFS_LINUX_TEST_SRC([blk_queue_flag_set], [
		#include <linux/kernel.h>
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		blk_queue_flag_set(0, q);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_FLAG_SET], [
	AC_MSG_CHECKING([whether blk_queue_flag_set() exists])
	ZFS_LINUX_TEST_RESULT([blk_queue_flag_set], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_FLAG_SET, 1,
		    [blk_queue_flag_set() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_CLEAR], [
	ZFS_LINUX_TEST_SRC([blk_queue_flag_clear], [
		#include <linux/kernel.h>
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		blk_queue_flag_clear(0, q);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_FLAG_CLEAR], [
	AC_MSG_CHECKING([whether blk_queue_flag_clear() exists])
	ZFS_LINUX_TEST_RESULT([blk_queue_flag_clear], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_FLAG_CLEAR, 1,
		    [blk_queue_flag_clear() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAGS], [
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_CLEAR
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_FLAGS], [
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_CLEAR
])
