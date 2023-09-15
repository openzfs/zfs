dnl #
dnl # 6.6 API change,
dnl # fsync_bdev was removed in favor of sync_blockdev
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SYNC_BDEV], [
	ZFS_LINUX_TEST_SRC([fsync_bdev], [
		#include <linux/blkdev.h>
	],[
		fsync_bdev(NULL);
	])

	ZFS_LINUX_TEST_SRC([sync_blockdev], [
		#include <linux/blkdev.h>
	],[
		sync_blockdev(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SYNC_BDEV], [
	AC_MSG_CHECKING([whether fsync_bdev() exists])
	ZFS_LINUX_TEST_RESULT([fsync_bdev], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FSYNC_BDEV, 1,
		    [fsync_bdev() is declared in include/blkdev.h])
	],[
		AC_MSG_CHECKING([whether sync_blockdev() exists])
		ZFS_LINUX_TEST_RESULT([sync_blockdev], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SYNC_BLOCKDEV, 1,
			    [sync_blockdev() is declared in include/blkdev.h])
		],[
			ZFS_LINUX_TEST_ERROR(
			    [neither fsync_bdev() nor sync_blockdev() exist])
		])
	])
])
