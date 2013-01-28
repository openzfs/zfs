dnl #
dnl # 2.6.27 API change
dnl # lookup_bdev() was exported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_LOOKUP_BDEV],
	[AC_MSG_CHECKING([whether lookup_bdev() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		lookup_bdev(NULL);
	], [lookup_bdev], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LOOKUP_BDEV, 1, [lookup_bdev() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
