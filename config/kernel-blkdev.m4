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

dnl #
dnl # 6.5.x API change,
dnl # blkdev_get_by_path() takes 4 args
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_GET_BY_PATH_4ARG], [
	ZFS_LINUX_TEST_SRC([blkdev_get_by_path_4arg], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		const char *path = "path";
		fmode_t mode = 0;
		void *holder = NULL;
		struct blk_holder_ops h;

		bdev = blkdev_get_by_path(path, mode, holder, &h);
	])
])

dnl #
dnl # 6.8.x API change
dnl # bdev_open_by_path() replaces blkdev_get_by_path()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_OPEN_BY_PATH], [
	ZFS_LINUX_TEST_SRC([bdev_open_by_path], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct bdev_handle *bdh __attribute__ ((unused)) = NULL;
		const char *path = "path";
		fmode_t mode = 0;
		void *holder = NULL;
		struct blk_holder_ops h;

		bdh = bdev_open_by_path(path, mode, holder, &h);
	])
])

dnl #
dnl # 6.9.x API change
dnl # bdev_file_open_by_path() replaced bdev_open_by_path(),
dnl # and returns struct file*
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BDEV_FILE_OPEN_BY_PATH], [
	ZFS_LINUX_TEST_SRC([bdev_file_open_by_path], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct file *file __attribute__ ((unused)) = NULL;
		const char *path = "path";
		fmode_t mode = 0;
		void *holder = NULL;
		struct blk_holder_ops h;

		file = bdev_file_open_by_path(path, mode, holder, &h);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH], [
	AC_MSG_CHECKING([whether blkdev_get_by_path() exists and takes 3 args])
	ZFS_LINUX_TEST_RESULT([blkdev_get_by_path], [
		AC_MSG_RESULT(yes)
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether blkdev_get_by_path() exists and takes 4 args])
		ZFS_LINUX_TEST_RESULT([blkdev_get_by_path_4arg], [
			AC_DEFINE(HAVE_BLKDEV_GET_BY_PATH_4ARG, 1,
				[blkdev_get_by_path() exists and takes 4 args])
			AC_MSG_RESULT(yes)
		], [
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether bdev_open_by_path() exists])
			ZFS_LINUX_TEST_RESULT([bdev_open_by_path], [
				AC_DEFINE(HAVE_BDEV_OPEN_BY_PATH, 1,
					[bdev_open_by_path() exists])
				AC_MSG_RESULT(yes)
			], [
				AC_MSG_RESULT(no)
				AC_MSG_CHECKING([whether bdev_file_open_by_path() exists])
				ZFS_LINUX_TEST_RESULT([bdev_file_open_by_path], [
					AC_DEFINE(HAVE_BDEV_FILE_OPEN_BY_PATH, 1,
						[bdev_file_open_by_path() exists])
					AC_MSG_RESULT(yes)
				], [
					AC_MSG_RESULT(no)
					ZFS_LINUX_TEST_ERROR([blkdev_get_by_path()])
				])
			])
		])
	])
])

dnl #
dnl # 6.5.x API change
dnl # blk_mode_t was added as a type to supercede some places where fmode_t
dnl # is used
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BLK_MODE_T], [
	ZFS_LINUX_TEST_SRC([blk_mode_t], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		blk_mode_t m __attribute((unused)) = (blk_mode_t)0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BLK_MODE_T], [
	AC_MSG_CHECKING([whether blk_mode_t is defined])
	ZFS_LINUX_TEST_RESULT([blk_mode_t], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_MODE_T, 1, [blk_mode_t is defined])
	], [
		AC_MSG_RESULT(no)
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

dnl #
dnl # 6.5.x API change.
dnl # blkdev_put() takes (void* holder) as arg 2
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_PUT_HOLDER], [
	ZFS_LINUX_TEST_SRC([blkdev_put_holder], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		void *holder = NULL;

		blkdev_put(bdev, holder);
	])
])

dnl #
dnl # 6.8.x API change
dnl # bdev_release() replaces blkdev_put()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_RELEASE], [
	ZFS_LINUX_TEST_SRC([bdev_release], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct bdev_handle *bdh = NULL;
		bdev_release(bdh);
	])
])

dnl #
dnl # 6.9.x API change
dnl #
dnl # bdev_release() now private, but because bdev_file_open_by_path() returns
dnl # struct file*, we can just use fput(). So the blkdev_put test no longer
dnl # fails if not found.
dnl #

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_PUT], [
	AC_MSG_CHECKING([whether blkdev_put() exists])
	ZFS_LINUX_TEST_RESULT([blkdev_put], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_PUT, 1, [blkdev_put() exists])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether blkdev_put() accepts void* as arg 2])
		ZFS_LINUX_TEST_RESULT([blkdev_put_holder], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLKDEV_PUT_HOLDER, 1,
				[blkdev_put() accepts void* as arg 2])
		], [
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether bdev_release() exists])
			ZFS_LINUX_TEST_RESULT([bdev_release], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_BDEV_RELEASE, 1,
					[bdev_release() exists])
			], [
				AC_MSG_RESULT(no)
			])
		])
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
dnl # 6.5.x API change
dnl # disk_check_media_change() was added
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_DISK_CHECK_MEDIA_CHANGE], [
	ZFS_LINUX_TEST_SRC([disk_check_media_change], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		bool error;

		error = disk_check_media_change(bdev->bd_disk);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_DISK_CHECK_MEDIA_CHANGE], [
	AC_MSG_CHECKING([whether disk_check_media_change() exists])
	ZFS_LINUX_TEST_RESULT([disk_check_media_change], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DISK_CHECK_MEDIA_CHANGE, 1,
		    [disk_check_media_change() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # bdev_kobj() is introduced from 5.12
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_KOBJ], [
	ZFS_LINUX_TEST_SRC([bdev_kobj], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
		#include <linux/kobject.h>
	], [
		struct block_device *bdev = NULL;
		struct kobject *disk_kobj;
		disk_kobj = bdev_kobj(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEV_KOBJ], [
	AC_MSG_CHECKING([whether bdev_kobj() exists])
	ZFS_LINUX_TEST_RESULT([bdev_kobj], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_KOBJ, 1,
		    [bdev_kobj() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # part_to_dev() was removed in 5.12
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_PART_TO_DEV], [
	ZFS_LINUX_TEST_SRC([part_to_dev], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct hd_struct *p = NULL;
		struct device *pdev;
		pdev = part_to_dev(p);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_PART_TO_DEV], [
	AC_MSG_CHECKING([whether part_to_dev() exists])
	ZFS_LINUX_TEST_RESULT([part_to_dev], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PART_TO_DEV, 1,
		    [part_to_dev() exists])
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
			ZFS_LINUX_TEST_ERROR([lookup_bdev()])
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
dnl # 5.16 API change
dnl # Added bdev_nr_bytes() helper.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_NR_BYTES], [
	ZFS_LINUX_TEST_SRC([bdev_nr_bytes], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		loff_t nr_bytes __attribute__ ((unused)) = 0;
		nr_bytes = bdev_nr_bytes(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEV_NR_BYTES], [
	AC_MSG_CHECKING([whether bdev_nr_bytes() is available])
	ZFS_LINUX_TEST_RESULT([bdev_nr_bytes], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_NR_BYTES, 1, [bdev_nr_bytes() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 5.20 API change,
dnl # Removed bdevname(), snprintf(.., %pg) should be used.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BDEVNAME], [
	ZFS_LINUX_TEST_SRC([bdevname], [
		#include <linux/fs.h>
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		char path[BDEVNAME_SIZE];

		(void) bdevname(bdev, path);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BDEVNAME], [
	AC_MSG_CHECKING([whether bdevname() exists])
	ZFS_LINUX_TEST_RESULT([bdevname], [
		AC_DEFINE(HAVE_BDEVNAME, 1, [bdevname() is available])
		AC_MSG_RESULT(yes)
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # TRIM support: discard and secure erase. We make use of asynchronous
dnl #               functions when available.
dnl #
dnl # 3.10:
dnl #   sync discard:  blkdev_issue_discard(..., 0)
dnl #   sync erase:    blkdev_issue_discard(..., BLKDEV_DISCARD_SECURE)
dnl #   async discard: [not available]
dnl #   async erase:   [not available]
dnl #
dnl # 4.7:
dnl #   sync discard:  blkdev_issue_discard(..., 0)
dnl #   sync erase:    blkdev_issue_discard(..., BLKDEV_DISCARD_SECURE)
dnl #   async discard: __blkdev_issue_discard(..., 0)
dnl #   async erase:   __blkdev_issue_discard(..., BLKDEV_DISCARD_SECURE)
dnl #
dnl # 5.19:
dnl #   sync discard:  blkdev_issue_discard(...)
dnl #   sync erase:    blkdev_issue_secure_erase(...)
dnl #   async discard: __blkdev_issue_discard(...)
dnl #   async erase:   [not available]
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_ISSUE_DISCARD], [
	ZFS_LINUX_TEST_SRC([blkdev_issue_discard_noflags], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		sector_t sector = 0;
		sector_t nr_sects = 0;
		int error __attribute__ ((unused));

		error = blkdev_issue_discard(bdev,
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
	ZFS_LINUX_TEST_SRC([blkdev_issue_discard_async_noflags], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		sector_t sector = 0;
		sector_t nr_sects = 0;
		struct bio *biop = NULL;
		int error __attribute__ ((unused));

		error = __blkdev_issue_discard(bdev,
		    sector, nr_sects, GFP_KERNEL, &biop);
	])
	ZFS_LINUX_TEST_SRC([blkdev_issue_discard_async_flags], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		sector_t sector = 0;
		sector_t nr_sects = 0;
		unsigned long flags = 0;
		struct bio *biop = NULL;
		int error __attribute__ ((unused));

		error = __blkdev_issue_discard(bdev,
		    sector, nr_sects, GFP_KERNEL, flags, &biop);
	])
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
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_ISSUE_DISCARD], [
	AC_MSG_CHECKING([whether blkdev_issue_discard() is available])
	ZFS_LINUX_TEST_RESULT([blkdev_issue_discard_noflags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_ISSUE_DISCARD_NOFLAGS, 1,
		    [blkdev_issue_discard() is available])
	],[
		AC_MSG_RESULT(no)
	])
	AC_MSG_CHECKING([whether blkdev_issue_discard(flags) is available])
	ZFS_LINUX_TEST_RESULT([blkdev_issue_discard_flags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_ISSUE_DISCARD_FLAGS, 1,
		    [blkdev_issue_discard(flags) is available])
	],[
		AC_MSG_RESULT(no)
	])
	AC_MSG_CHECKING([whether __blkdev_issue_discard() is available])
	ZFS_LINUX_TEST_RESULT([blkdev_issue_discard_async_noflags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_ISSUE_DISCARD_ASYNC_NOFLAGS, 1,
		    [__blkdev_issue_discard() is available])
	],[
		AC_MSG_RESULT(no)
	])
	AC_MSG_CHECKING([whether __blkdev_issue_discard(flags) is available])
	ZFS_LINUX_TEST_RESULT([blkdev_issue_discard_async_flags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_ISSUE_DISCARD_ASYNC_FLAGS, 1,
		    [__blkdev_issue_discard(flags) is available])
	],[
		AC_MSG_RESULT(no)
	])
	AC_MSG_CHECKING([whether blkdev_issue_secure_erase() is available])
	ZFS_LINUX_TEST_RESULT([blkdev_issue_secure_erase], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_ISSUE_SECURE_ERASE, 1,
		    [blkdev_issue_secure_erase() is available])
	],[
		AC_MSG_RESULT(no)
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

dnl #
dnl # 6.5.x API change
dnl # BLK_STS_NEXUS replaced with BLK_STS_RESV_CONFLICT
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_BLK_STS_RESV_CONFLICT], [
	ZFS_LINUX_TEST_SRC([blk_sts_resv_conflict], [
		#include <linux/blkdev.h>
	],[
		blk_status_t s __attribute__ ((unused)) = BLK_STS_RESV_CONFLICT;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_BLK_STS_RESV_CONFLICT], [
	AC_MSG_CHECKING([whether BLK_STS_RESV_CONFLICT is defined])
		ZFS_LINUX_TEST_RESULT([blk_sts_resv_conflict], [
			AC_DEFINE(HAVE_BLK_STS_RESV_CONFLICT, 1, [BLK_STS_RESV_CONFLICT is defined])
			AC_MSG_RESULT(yes)
		], [
			AC_MSG_RESULT(no)
		])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV], [
	ZFS_AC_KERNEL_SRC_BLKDEV_GET_BY_PATH
	ZFS_AC_KERNEL_SRC_BLKDEV_GET_BY_PATH_4ARG
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_OPEN_BY_PATH
	ZFS_AC_KERNEL_SRC_BDEV_FILE_OPEN_BY_PATH
	ZFS_AC_KERNEL_SRC_BLKDEV_PUT
	ZFS_AC_KERNEL_SRC_BLKDEV_PUT_HOLDER
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_RELEASE
	ZFS_AC_KERNEL_SRC_BLKDEV_REREAD_PART
	ZFS_AC_KERNEL_SRC_BLKDEV_INVALIDATE_BDEV
	ZFS_AC_KERNEL_SRC_BLKDEV_LOOKUP_BDEV
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_PHYSICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_SRC_BLKDEV_CHECK_DISK_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_WHOLE
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_NR_BYTES
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEVNAME
	ZFS_AC_KERNEL_SRC_BLKDEV_ISSUE_DISCARD
	ZFS_AC_KERNEL_SRC_BLKDEV_BDEV_KOBJ
	ZFS_AC_KERNEL_SRC_BLKDEV_PART_TO_DEV
	ZFS_AC_KERNEL_SRC_BLKDEV_DISK_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_SRC_BLKDEV_BLK_STS_RESV_CONFLICT
	ZFS_AC_KERNEL_SRC_BLKDEV_BLK_MODE_T
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
	ZFS_AC_KERNEL_BLKDEV_BDEV_NR_BYTES
	ZFS_AC_KERNEL_BLKDEV_BDEVNAME
	ZFS_AC_KERNEL_BLKDEV_GET_ERESTARTSYS
	ZFS_AC_KERNEL_BLKDEV_ISSUE_DISCARD
	ZFS_AC_KERNEL_BLKDEV_BDEV_KOBJ
	ZFS_AC_KERNEL_BLKDEV_PART_TO_DEV
	ZFS_AC_KERNEL_BLKDEV_DISK_CHECK_MEDIA_CHANGE
	ZFS_AC_KERNEL_BLKDEV_BLK_STS_RESV_CONFLICT
	ZFS_AC_KERNEL_BLKDEV_BLK_MODE_T
])
