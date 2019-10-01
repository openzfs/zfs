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
		AC_DEFINE(HAVE_BLK_QUEUE_DISCARD, 1,
		    [blk_queue_discard() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.8 - 4.x API,
dnl #   blk_queue_secure_erase()
dnl #
dnl # 2.6.36 - 4.7 API,
dnl #   blk_queue_secdiscard()
dnl #
dnl # 2.6.x - 2.6.35 API,
dnl #   Unsupported by kernel
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
			AC_MSG_RESULT(no)
		])
	])
])
