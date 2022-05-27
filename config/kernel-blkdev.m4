dnl #
dnl # 2.6.38 API change,
dnl # Added blkdev_get_by_path()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_GET_BY_PATH], [
	ZFS_LINUX_TEST_SRC([blkdev_get_by_path], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		const char *path = "path";
		fmode_t mode = 0;
		void *holder = NULL;

		bdev = blkdev_get_by_path(path, mode, holder);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH], [
	AC_MSG_CHECKING([whether blkdev_get_by_path() exists])
	ZFS_LINUX_TEST_RESULT([blkdev_get_by_path], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([blkdev_get_by_path()])
	])
])

dnl #
dnl # 2.6.38 API change,
dnl # Added blkdev_put()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_PUT], [
	ZFS_LINUX_TEST_SRC([blkdev_put], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		fmode_t mode = 0;

		blkdev_put(bdev, mode);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_PUT], [
	AC_MSG_CHECKING([whether blkdev_put() exists])
	ZFS_LINUX_TEST_RESULT([blkdev_put], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([blkdev_put()])
	])
])

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
	AC_MSG_CHECKING([whether bdev_check_media_change() exists])
	ZFS_LINUX_TEST_RESULT([bdev_check_media_change], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_CHECK_MEDIA_CHANGE, 1,
		    [bdev_check_media_change() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.22 API change
dnl # Single argument invalidate_bdev()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_INVALIDATE_BDEV], [
	ZFS_LINUX_TEST_SRC([invalidate_bdev], [
		#include <linux/buffer_head.h>
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		invalidate_bdev(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_INVALIDATE_BDEV], [
	AC_MSG_CHECKING([whether invalidate_bdev() exists])
	ZFS_LINUX_TEST_RESULT([invalidate_bdev], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([invalidate_bdev()])
	])
])

dnl #
dnl # 5.11 API, lookup_bdev() takes dev_t argument.
dnl # 2.6.27 API, lookup_bdev() was first exported.
dnl # 4.4.0-6.21 API, lookup_bdev() on Ubuntu takes mode argument.
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

	ZFS_LINUX_TEST_SRC([lookup_bdev_mode], [
		#include <linux/fs.h>
	], [
		struct block_device *bdev __attribute__ ((unused));
		const char path[] = "/example/path";

		bdev = lookup_bdev(path, FMODE_READ);
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

			AC_MSG_CHECKING([whether lookup_bdev() wants mode arg])
			ZFS_LINUX_TEST_RESULT_SYMBOL([lookup_bdev_mode],
			    [lookup_bdev], [fs/block_dev.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_MODE_LOOKUP_BDEV, 1,
				    [lookup_bdev() wants mode arg])
			], [
				ZFS_LINUX_TEST_ERROR([lookup_bdev()])
			])
		])
	])
])

dnl #
dnl # 2.6.30 API change
dnl #
dnl # The bdev_physical_block_size() interface was added to provide a way
dnl # to determine the smallest write which can be performed without a
dnl # read-modify-write operation.
dnl #
dnl # Unfortunately, this interface isn't entirely reliable because
dnl # drives are sometimes known to misreport this value.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_PHYSICAL_BLOCK_SIZE], [
	ZFS_LINUX_TEST_SRC([bdev_physical_block_size], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		bdev_physical_block_size(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEV_PHYSICAL_BLOCK_SIZE], [
	AC_MSG_CHECKING([whether bdev_physical_block_size() is available])
	ZFS_LINUX_TEST_RESULT([bdev_physical_block_size], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([bdev_physical_block_size()])
	])
])

dnl #
dnl # 2.6.30 API change
dnl # Added bdev_logical_block_size().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_LOGICAL_BLOCK_SIZE], [
	ZFS_LINUX_TEST_SRC([bdev_logical_block_size], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		bdev_logical_block_size(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEV_LOGICAL_BLOCK_SIZE], [
	AC_MSG_CHECKING([whether bdev_logical_block_size() is available])
	ZFS_LINUX_TEST_RESULT([bdev_logical_block_size], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([bdev_logical_block_size()])
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

dnl #
dnl # 5.19 API: blkdev_issue_secure_erase()
dnl # 3.10 API: blkdev_issue_discard(..., BLKDEV_DISCARD_SECURE)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_ISSUE_SECURE_ERASE], [
	ZFS_LINUX_TEST_SRC([blkdev_issue_secure_erase], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		sector_t sector = 0;
		sector_t nr_sects = 0;
		int error __attribute__ ((unused));

		error = blkdev_issue_secure_erase(bdev,
		    sector, nr_sects, GFP_KERNEL);
	])

	ZFS_LINUX_TEST_SRC([blkdev_issue_discard_flags], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		sector_t sector = 0;
		sector_t nr_sects = 0;
		unsigned long flags = 0;
		int error __attribute__ ((unused));

		error = blkdev_issue_discard(bdev,
		    sector, nr_sects, GFP_KERNEL, flags);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_ISSUE_SECURE_ERASE], [
	AC_MSG_CHECKING([whether blkdev_issue_secure_erase() is available])
	ZFS_LINUX_TEST_RESULT([blkdev_issue_secure_erase], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_ISSUE_SECURE_ERASE, 1,
		    [blkdev_issue_secure_erase() is available])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether blkdev_issue_discard() is available])
		ZFS_LINUX_TEST_RESULT([blkdev_issue_discard_flags], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLKDEV_ISSUE_DISCARD, 1,
			    [blkdev_issue_discard() is available])
		],[
			ZFS_LINUX_TEST_ERROR([blkdev_issue_discard()])
		])
	])
])

dnl #
dnl # 5.13 API change
dnl # blkdev_get_by_path() no longer handles ERESTARTSYS
dnl #
dnl # Unfortunately we're forced to rely solely on the kernel version
dnl # number in order to determine the expected behavior.  This was an
dnl # internal change to blkdev_get_by_dev(), see commit a8ed1a0607.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_GET_ERESTARTSYS], [
	AC_MSG_CHECKING([whether blkdev_get_by_path() handles ERESTARTSYS])
	AS_VERSION_COMPARE([$LINUX_VERSION], [5.13.0], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_GET_ERESTARTSYS, 1,
			[blkdev_get_by_path() handles ERESTARTSYS])
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV], [
	ZFS_AC_KERNEL_SRC_BLKDEV_GET_BY_PATH
	ZFS_AC_KERNEL_SRC_BLKDEV_PUT
	ZFS_AC_KERNEL_SRC_BLKDEV_REREAD_PART
	ZFS_AC_KERNEL_SRC_BLKDEV_INVALIDATE_BDEV
	ZFS_AC_KERNEL_SRC_BLKDEV_LOOKUP_BDEV
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_PHYSICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_SRC_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_WHOLE
	ZFS_AC_KERNEL_SRC_BLKDEV_ISSUE_SECURE_ERASE
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV], [
	ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH
	ZFS_AC_KERNEL_BLKDEV_PUT
	ZFS_AC_KERNEL_BLKDEV_REREAD_PART
	ZFS_AC_KERNEL_BLKDEV_INVALIDATE_BDEV
	ZFS_AC_KERNEL_BLKDEV_LOOKUP_BDEV
	ZFS_AC_KERNEL_BLKDEV_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BLKDEV_BDEV_PHYSICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_BLKDEV_BDEV_WHOLE
	ZFS_AC_KERNEL_BLKDEV_GET_ERESTARTSYS
	ZFS_AC_KERNEL_BLKDEV_ISSUE_SECURE_ERASE
])
