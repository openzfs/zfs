dnl #
dnl # 2.6.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GET_DISK_RO], [
	ZFS_LINUX_TEST_SRC([get_disk_ro], [
		#include <linux/blkdev.h>
	],[
		struct gendisk *disk = NULL;
		(void) get_disk_ro(disk);
	], [$NO_UNUSED_BUT_SET_VARIABLE])
])

AC_DEFUN([ZFS_AC_KERNEL_GET_DISK_RO], [
	AC_MSG_CHECKING([whether get_disk_ro() is available])
	ZFS_LINUX_TEST_RESULT([get_disk_ro], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([get_disk_ro()])
	])
])
