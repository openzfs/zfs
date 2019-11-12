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

dnl #
dnl # 2.6.32 - 4.x API,
dnl #   blk_queue_discard()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_DISCARD], [
	ZFS_LINUX_TEST_SRC([blk_queue_discard], [
		#include <linux/blkdev.h>
	],[
		struct request_queue *q __attribute__ ((unused)) = NULL;
		int value __attribute__ ((unused));
		value = blk_queue_discard(q);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_DISCARD], [
	AC_MSG_CHECKING([whether blk_queue_discard() is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_discard], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([blk_queue_discard])
	])
])

dnl #
dnl # 4.8 - 4.x API,
dnl #   blk_queue_secure_erase()
dnl #
dnl # 2.6.36 - 4.7 API,
dnl #   blk_queue_secdiscard()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_SECURE_ERASE], [
	ZFS_LINUX_TEST_SRC([blk_queue_secure_erase], [
		#include <linux/blkdev.h>
	],[
		struct request_queue *q __attribute__ ((unused)) = NULL;
		int value __attribute__ ((unused));
		value = blk_queue_secure_erase(q);
	])

	ZFS_LINUX_TEST_SRC([blk_queue_secdiscard], [
		#include <linux/blkdev.h>
	],[
		struct request_queue *q __attribute__ ((unused)) = NULL;
		int value __attribute__ ((unused));
		value = blk_queue_secdiscard(q);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_SECURE_ERASE], [
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
		struct request_queue *q = NULL;
		(void) blk_queue_flush(q, REQ_FLUSH);
	], [$NO_UNUSED_BUT_SET_VARIABLE], [$ZFS_META_LICENSE])

	ZFS_LINUX_TEST_SRC([blk_queue_write_cache], [
		#include <linux/kernel.h>
		#include <linux/blkdev.h>
	], [
		struct request_queue *q = NULL;
		blk_queue_write_cache(q, true, true);
	], [$NO_UNUSED_BUT_SET_VARIABLE], [$ZFS_META_LICENSE])
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
		struct request_queue *q = NULL;
		(void) blk_queue_max_hw_sectors(q, BLK_SAFE_MAX_SECTORS);
	], [$NO_UNUSED_BUT_SET_VARIABLE])
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
		struct request_queue *q = NULL;
		(void) blk_queue_max_segments(q, BLK_MAX_SEGMENTS);
	], [$NO_UNUSED_BUT_SET_VARIABLE])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS], [
	AC_MSG_CHECKING([whether blk_queue_max_segments() is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_max_segments], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([blk_queue_max_segments])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE], [
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_PLUG
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_BDI
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_DISCARD
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_SECURE_ERASE
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLAG_CLEAR
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_SRC_BLK_QUEUE_MAX_SEGMENTS
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE], [
	ZFS_AC_KERNEL_BLK_QUEUE_PLUG
	ZFS_AC_KERNEL_BLK_QUEUE_BDI
	ZFS_AC_KERNEL_BLK_QUEUE_DISCARD
	ZFS_AC_KERNEL_BLK_QUEUE_SECURE_ERASE
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_CLEAR
	ZFS_AC_KERNEL_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS
])
