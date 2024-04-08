dnl #
dnl # 2.6.39 API change,
dnl # blk_start_plug() and blk_finish_plug()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_PLUG], [
	ZFS_LINUX_TEST_SRC([blk_plug], [
		#include <linux/blkdev.h>
	],[
		struct blk_plug plug __attribute__ ((unused));

		blk_start_plug(&plug);
		blk_finish_plug(&plug);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_PLUG], [
	AC_MSG_CHECKING([whether struct blk_plug is available])
	ZFS_LINUX_TEST_RESULT([blk_plug], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([blk_plug])
	])
])

dnl #
dnl # 2.6.32 - 4.11: statically allocated bdi in request_queue
dnl # 4.12: dynamically allocated bdi in request_queue
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

dnl #
dnl # 5.9: added blk_queue_update_readahead(),
dnl # 5.15: renamed to disk_update_readahead()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_UPDATE_READAHEAD], [
	ZFS_LINUX_TEST_SRC([blk_queue_update_readahead], [
		#include <linux/blkdev.h>
	],[
		struct request_queue q;
		blk_queue_update_readahead(&q);
	])

	ZFS_LINUX_TEST_SRC([disk_update_readahead], [
		#include <linux/blkdev.h>
	],[
		struct gendisk disk;
		disk_update_readahead(&disk);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_UPDATE_READAHEAD], [
	AC_MSG_CHECKING([whether blk_queue_update_readahead() exists])
	ZFS_LINUX_TEST_RESULT([blk_queue_update_readahead], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_UPDATE_READAHEAD, 1,
		    [blk_queue_update_readahead() exists])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether disk_update_readahead() exists])
		ZFS_LINUX_TEST_RESULT([disk_update_readahead], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_DISK_UPDATE_READAHEAD, 1,
			    [disk_update_readahead() exists])
		],[
			AC_MSG_RESULT(no)
		])
	])
])

dnl #
dnl # 5.19: bdev_max_discard_sectors() available
dnl # 2.6.32: blk_queue_discard() available
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_DISCARD], [
	ZFS_LINUX_TEST_SRC([bdev_max_discard_sectors], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		unsigned int error __attribute__ ((unused));

		error = bdev_max_discard_sectors(bdev);
	])

	ZFS_LINUX_TEST_SRC([blk_queue_discard], [
		#include <linux/blkdev.h>
	],[
		struct request_queue r;
		struct request_queue *q = &r;
		int value __attribute__ ((unused));
		memset(q, 0, sizeof(r));
		value = blk_queue_discard(q);
	],[-Wframe-larger-than=8192])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_DISCARD], [
	AC_MSG_CHECKING([whether bdev_max_discard_sectors() is available])
	ZFS_LINUX_TEST_RESULT([bdev_max_discard_sectors], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_MAX_DISCARD_SECTORS, 1,
		    [bdev_max_discard_sectors() is available])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether blk_queue_discard() is available])
		ZFS_LINUX_TEST_RESULT([blk_queue_discard], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_DISCARD, 1,
			    [blk_queue_discard() is available])
		],[
			ZFS_LINUX_TEST_ERROR([blk_queue_discard])
		])
	])
])

dnl #
dnl # 5.19: bdev_max_secure_erase_sectors() available
dnl # 4.8: blk_queue_secure_erase() available
dnl # 2.6.36: blk_queue_secdiscard() available
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_SECURE_ERASE], [
	ZFS_LINUX_TEST_SRC([bdev_max_secure_erase_sectors], [
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev __attribute__ ((unused)) = NULL;
		unsigned int error __attribute__ ((unused));

		error = bdev_max_secure_erase_sectors(bdev);
	])

	ZFS_LINUX_TEST_SRC([blk_queue_secure_erase], [
		#include <linux/blkdev.h>
	],[
		struct request_queue r;
		struct request_queue *q = &r;
		int value __attribute__ ((unused));
		memset(q, 0, sizeof(r));
		value = blk_queue_secure_erase(q);
	],[-Wframe-larger-than=8192])

	ZFS_LINUX_TEST_SRC([blk_queue_secdiscard], [
		#include <linux/blkdev.h>
	],[
		struct request_queue r;
		struct request_queue *q = &r;
		int value __attribute__ ((unused));
		memset(q, 0, sizeof(r));
		value = blk_queue_secdiscard(q);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_SECURE_ERASE], [
	AC_MSG_CHECKING([whether bdev_max_secure_erase_sectors() is available])
	ZFS_LINUX_TEST_RESULT([bdev_max_secure_erase_sectors], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_MAX_SECURE_ERASE_SECTORS, 1,
		    [bdev_max_secure_erase_sectors() is available])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether blk_queue_secure_erase() is available])
		ZFS_LINUX_TEST_RESULT([blk_queue_secure_erase], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_SECURE_ERASE, 1,
			    [blk_queue_secure_erase() is available])
		],[
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([whether blk_queue_secdiscard() is available])
			ZFS_LINUX_TEST_RESULT([blk_queue_secdiscard], [
				AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_SECDISCARD, 1,
				    [blk_queue_secdiscard() is available])
			],[
				ZFS_LINUX_TEST_ERROR([blk_queue_secure_erase])
			])
		])
	])
])

dnl #
dnl # 4.16 API change,
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

dnl #
dnl # 2.6.36 API change,
dnl # Added blk_queue_flush() interface, while the previous interface
dnl # was available to all the new one is GPL-only.  Thus in addition to
dnl # detecting if this function is available we determine if it is
dnl # GPL-only.  If the GPL-only interface is there we implement our own
dnl # compatibility function, otherwise we use the function.  The hope
dnl # is that long term this function will be opened up.
dnl #
dnl # 4.7 API change,
dnl # Replace blk_queue_flush with blk_queue_write_cache
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLUSH], [
	ZFS_LINUX_TEST_SRC([blk_queue_flush], [
		#include <linux/blkdev.h>
	], [
		struct request_queue *q __attribute__ ((unused)) = NULL;
		(void) blk_queue_flush(q, REQ_FLUSH);
	], [], [ZFS_META_LICENSE])

	ZFS_LINUX_TEST_SRC([blk_queue_write_cache], [
		#include <linux/kernel.h>
		#include <linux/blkdev.h>
	], [
		struct request_queue *q __attribute__ ((unused)) = NULL;
		blk_queue_write_cache(q, true, true);
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_FLUSH], [
	AC_MSG_CHECKING([whether blk_queue_flush() is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_flush], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_FLUSH, 1,
		    [blk_queue_flush() is available])

		AC_MSG_CHECKING([whether blk_queue_flush() is GPL-only])
		ZFS_LINUX_TEST_RESULT([blk_queue_flush_license], [
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_FLUSH_GPL_ONLY, 1,
			    [blk_queue_flush() is GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])

	dnl #
	dnl # 4.7 API change
	dnl # Replace blk_queue_flush with blk_queue_write_cache
	dnl #
	AC_MSG_CHECKING([whether blk_queue_write_cache() exists])
	ZFS_LINUX_TEST_RESULT([blk_queue_write_cache], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_WRITE_CACHE, 1,
		    [blk_queue_write_cache() exists])

		AC_MSG_CHECKING([whether blk_queue_write_cache() is GPL-only])
		ZFS_LINUX_TEST_RESULT([blk_queue_write_cache_license], [
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_WRITE_CACHE_GPL_ONLY, 1,
			    [blk_queue_write_cache() is GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.34 API change
dnl # blk_queue_max_hw_sectors() replaces blk_queue_max_sectors().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_HW_SECTORS], [
	ZFS_LINUX_TEST_SRC([blk_queue_max_hw_sectors], [
		#include <linux/blkdev.h>
	], [
		struct request_queue *q __attribute__ ((unused)) = NULL;
		(void) blk_queue_max_hw_sectors(q, BLK_SAFE_MAX_SECTORS);
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS], [
	AC_MSG_CHECKING([whether blk_queue_max_hw_sectors() is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_max_hw_sectors], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([blk_queue_max_hw_sectors])
	])
])

dnl #
dnl # 2.6.34 API change
dnl # blk_queue_max_segments() consolidates blk_queue_max_hw_segments()
dnl # and blk_queue_max_phys_segments().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_SEGMENTS], [
	ZFS_LINUX_TEST_SRC([blk_queue_max_segments], [
		#include <linux/blkdev.h>
	], [
		struct request_queue *q __attribute__ ((unused)) = NULL;
		(void) blk_queue_max_segments(q, BLK_MAX_SEGMENTS);
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS], [
	AC_MSG_CHECKING([whether blk_queue_max_segments() is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_max_segments], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([blk_queue_max_segments])
	])
])

dnl #
dnl # See if kernel supports block multi-queue and blk_status_t.
dnl # blk_status_t represents the new status codes introduced in the 4.13
dnl # kernel patch:
dnl #
dnl #  block: introduce new block status code type
dnl #
dnl # We do not currently support the "old" block multi-queue interfaces from
dnl # prior kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_MQ], [
	ZFS_LINUX_TEST_SRC([blk_mq], [
		#include <linux/blk-mq.h>
	], [
		struct blk_mq_tag_set tag_set __attribute__ ((unused)) = {0};
		(void) blk_mq_alloc_tag_set(&tag_set);
		return BLK_STS_OK;
	], [])
	ZFS_LINUX_TEST_SRC([blk_mq_rq_hctx], [
		#include <linux/blk-mq.h>
		#include <linux/blkdev.h>
	], [
		struct request rq = {0};
		struct blk_mq_hw_ctx *hctx = NULL;
		rq.mq_hctx = hctx;
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_MQ], [
	AC_MSG_CHECKING([whether block multiqueue with blk_status_t is available])
	ZFS_LINUX_TEST_RESULT([blk_mq], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_MQ, 1, [block multiqueue is available])
		AC_MSG_CHECKING([whether block multiqueue hardware context is cached in struct request])
		ZFS_LINUX_TEST_RESULT([blk_mq_rq_hctx], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_MQ_RQ_HCTX, 1, [block multiqueue hardware context is cached in struct request])
		], [
			AC_MSG_RESULT(no)
		])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE], [
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_PLUG
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_BDI
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_UPDATE_READAHEAD
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_DISCARD
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_SECURE_ERASE
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_CLEAR
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_SEGMENTS
	ZFS_AC_KERNEL_SRC_BLK_MQ
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE], [
	ZFS_AC_KERNEL_BLK_QUEUE_PLUG
	ZFS_AC_KERNEL_BLK_QUEUE_BDI
	ZFS_AC_KERNEL_BLK_QUEUE_UPDATE_READAHEAD
	ZFS_AC_KERNEL_BLK_QUEUE_DISCARD
	ZFS_AC_KERNEL_BLK_QUEUE_SECURE_ERASE
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_CLEAR
	ZFS_AC_KERNEL_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS
	ZFS_AC_KERNEL_BLK_MQ
])
