dnl #
dnl # check_disk_change() was removed in 5.10
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_CHECK_DISK_CHANGE], [
	ZFS_LINUX_TEST_SRC([check_disk_change], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		bool error;

		error = check_disk_change(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_CHECK_DISK_CHANGE], [
	AC_MSG_CHECKING([whether check_disk_change() exists])
	ZFS_LINUX_TEST_RESULT([check_disk_change], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CHECK_DISK_CHANGE, 1,
		    [check_disk_change() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 5.10 API, check_disk_change() is removed, in favor of
dnl # bdev_check_media_change(), which doesn't force revalidation
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_CHECK_MEDIA_CHANGE], [
	ZFS_LINUX_TEST_SRC([bdev_check_media_change], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		int error;

		error = bdev_check_media_change(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEV_CHECK_MEDIA_CHANGE], [
	AC_MSG_CHECKING([whether bdev_disk_changed() exists])
	ZFS_LINUX_TEST_RESULT([bdev_check_media_change], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_CHECK_MEDIA_CHANGE, 1,
		    [bdev_check_media_change() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_CHANGE], [
	ZFS_AC_KERNEL_SRC_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_CHANGE], [
	ZFS_AC_KERNEL_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
])
