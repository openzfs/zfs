dnl #
dnl # 2.6.28 API change
dnl # open/close_bdev_excl() renamed to open/close_bdev_exclusive()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BDEV_OPEN_EXCLUSIVE], [
	ZFS_LINUX_TEST_SRC([open_bdev_exclusive], [
		#include <linux/fs.h>
	], [
		open_bdev_exclusive(NULL, 0, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BDEV_OPEN_EXCLUSIVE], [
	AC_MSG_CHECKING([whether open_bdev_exclusive() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([open_bdev_exclusive],
	    [open_bdev_exclusive], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_OPEN_BDEV_EXCLUSIVE, 1,
		    [open_bdev_exclusive() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
