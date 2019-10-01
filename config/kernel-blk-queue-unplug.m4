dnl #
dnl # 2.6.32-2.6.35 API - The BIO_RW_UNPLUG enum can be used as a hint
dnl # to unplug the queue.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_RW_UNPLUG], [
	ZFS_LINUX_TEST_SRC([blk_queue_bio_rw_unplug], [
		#include <linux/blkdev.h>
	],[
		enum bio_rw_flags rw __attribute__ ((unused)) = BIO_RW_UNPLUG;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_RW_UNPLUG], [
	AC_MSG_CHECKING([whether the BIO_RW_UNPLUG enum is available])
	ZFS_LINUX_TEST_RESULT([blk_queue_bio_rw_unplug], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_HAVE_BIO_RW_UNPLUG, 1,
		    [BIO_RW_UNPLUG is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_PLUG], [
	ZFS_LINUX_TEST_SRC([blk_plug], [
		#include <linux/blkdev.h>
	],[
		struct blk_plug plug __attribute__ ((unused));

		blk_start_plug(&plug);
		blk_finish_plug(&plug);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_PLUG], [
	AC_MSG_CHECKING([whether struct blk_plug is available])
	ZFS_LINUX_TEST_RESULT([blk_plug], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_HAVE_BLK_PLUG, 1,
		    [struct blk_plug is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_QUEUE_PLUG], [
	ZFS_AC_KERNEL_SRC_BIO_RW_UNPLUG
	ZFS_AC_KERNEL_SRC_BLK_PLUG
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_PLUG], [
	ZFS_AC_KERNEL_BIO_RW_UNPLUG
	ZFS_AC_KERNEL_BLK_PLUG
])
