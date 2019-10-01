dnl #
dnl # 4.1 API, exported blkdev_reread_part() symbol, backported to the
dnl # 3.10.0 CentOS 7.x enterprise kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKDEV_REREAD_PART], [
	ZFS_LINUX_TEST_SRC([blkdev_reread_part], [
		#include <linux/fs.h>
	], [
		struct block_device *bdev = NULL;
		int error;

		error = blkdev_reread_part(bdev);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_REREAD_PART], [
	AC_MSG_CHECKING([whether blkdev_reread_part() is available])
	ZFS_LINUX_TEST_RESULT([blkdev_reread_part], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_REREAD_PART, 1,
		    [blkdev_reread_part() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
