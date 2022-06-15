dnl #
dnl # 5.16 API change
dnl # add_disk grew a must-check return code
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_ADD_DISK], [
	ZFS_LINUX_TEST_SRC([add_disk_ret], [
		#include <linux/blkdev.h>
	], [
		struct gendisk *disk = NULL;
		int error __attribute__ ((unused)) = add_disk(disk);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_ADD_DISK], [
	AC_MSG_CHECKING([whether add_disk() returns int])
	ZFS_LINUX_TEST_RESULT([add_disk_ret],
	[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ADD_DISK_RET, 1,
		    [add_disk() returns int])
	], [
		AC_MSG_RESULT(no)
	])
])
