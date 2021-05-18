dnl #
dnl # 4.1 API, exported blkdev_reread_part() symbol, back ported to the
dnl # 3.10.0 CentOS 7.x enterprise kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_REREAD_PART], [
	ZFS_LINUX_TEST_SRC([blkdev_reread_part], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		int error;

		error = blkdev_reread_part(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_REREAD_PART], [
	AC_MSG_CHECKING([whether blkdev_reread_part() exists])
	ZFS_LINUX_TEST_RESULT([blkdev_reread_part], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_REREAD_PART, 1,
		    [blkdev_reread_part() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

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

dnl #
dnl # 5.11 API, lookup_bdev() takes dev_t argument.
dnl # 4.4.0-6.21 API, lookup_bdev() on Ubuntu takes mode argument.
dnl # 2.6.27 API, lookup_bdev() was first exported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_LOOKUP_BDEV], [
	ZFS_LINUX_TEST_SRC([lookup_bdev_devt], [
		#include <linux/blkdev.h>
	], [
		int error __attribute__ ((unused));
		const char path[] = "/example/path";
		dev_t dev;

		error = lookup_bdev(path, &dev);
	])

	ZFS_LINUX_TEST_SRC([lookup_bdev_1arg], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev __attribute__ ((unused));
		const char path[] = "/example/path";

		bdev = lookup_bdev(path);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_LOOKUP_BDEV], [
	AC_MSG_CHECKING([whether lookup_bdev() wants dev_t arg])
	ZFS_LINUX_TEST_RESULT_SYMBOL([lookup_bdev_devt],
	    [lookup_bdev], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEVT_LOOKUP_BDEV, 1,
		    [lookup_bdev() wants dev_t arg])
	], [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether lookup_bdev() wants 1 arg])
		ZFS_LINUX_TEST_RESULT_SYMBOL([lookup_bdev_1arg],
		    [lookup_bdev], [fs/block_dev.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_1ARG_LOOKUP_BDEV, 1,
			    [lookup_bdev() wants 1 arg])
		], [
			AC_MSG_RESULT(no)

			AC_DEFINE(HAVE_MODE_LOOKUP_BDEV, 1,
			    [lookup_bdev() wants mode arg])
		])
	])
])

dnl #
dnl # 5.11 API change
dnl # Added bdev_whole() helper.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_WHOLE], [
	ZFS_LINUX_TEST_SRC([bdev_whole], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		bdev = bdev_whole(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEV_WHOLE], [
	AC_MSG_CHECKING([whether bdev_whole() is available])
	ZFS_LINUX_TEST_RESULT([bdev_whole], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_WHOLE, 1, [bdev_whole() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV], [
	ZFS_AC_KERNEL_SRC_BLKDEV_REREAD_PART
	ZFS_AC_KERNEL_SRC_BLKDEV_LOOKUP_BDEV
	ZFS_AC_KERNEL_SRC_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_WHOLE
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV], [
	ZFS_AC_KERNEL_BLKDEV_REREAD_PART
	ZFS_AC_KERNEL_BLKDEV_LOOKUP_BDEV
	ZFS_AC_KERNEL_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_BLKDEV_BDEV_WHOLE
])
