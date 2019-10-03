dnl #
dnl # 4.16 API change
dnl # Verify if get_disk_and_module() symbol is available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GET_DISK_AND_MODULE], [
	ZFS_LINUX_TEST_SRC([get_disk_and_module], [
		#include <linux/genhd.h>
	], [
		struct gendisk *disk = NULL;
		(void) get_disk_and_module(disk);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GET_DISK_AND_MODULE], [
	AC_MSG_CHECKING([whether get_disk_and_module() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([get_disk_and_module],
	    [get_disk_and_module], [block/genhd.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_DISK_AND_MODULE,
		    1, [get_disk_and_module() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
