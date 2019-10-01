dnl #
dnl # 2.6.27, lookup_bdev() was exported.
dnl # 4.4.0-6.21 - x.y on Ubuntu, lookup_bdev() takes 2 arguments.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_LOOKUP_BDEV], [
	ZFS_LINUX_TEST_SRC([lookup_bdev_1arg], [
		#include <linux/fs.h>
	], [
		lookup_bdev(NULL);
	])

	ZFS_LINUX_TEST_SRC([lookup_bdev_2args], [
		#include <linux/fs.h>
	], [
		lookup_bdev(NULL, FMODE_READ);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_LOOKUP_BDEV], [
	AC_MSG_CHECKING([whether lookup_bdev() wants 1 arg])
	ZFS_LINUX_TEST_RESULT_SYMBOL([lookup_bdev_1arg],
	    [lookup_bdev], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_LOOKUP_BDEV, 1,
		    [lookup_bdev() wants 1 arg])
	], [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether lookup_bdev() wants 2 args])
		ZFS_LINUX_TEST_RESULT_SYMBOL([lookup_bdev_2args],
		    [lookup_bdev], [fs/block_dev.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_2ARGS_LOOKUP_BDEV, 1,
			    [lookup_bdev() wants 2 args])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
