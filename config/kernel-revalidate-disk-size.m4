dnl #
dnl # 5.10 API change
dnl # revalidate_disk() was replaced by revalidate_disk_size()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_REVALIDATE_DISK_SIZE], [
	ZFS_LINUX_TEST_SRC([revalidate_disk_size], [
		#include <linux/genhd.h>
	], [
		struct gendisk *disk = NULL;
		(void) revalidate_disk_size(disk, false);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_REVALIDATE_DISK_SIZE], [
	AC_MSG_CHECKING([whether revalidate_disk_size() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([revalidate_disk_size],
		[revalidate_disk_size], [block/genhd.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REVALIDATE_DISK_SIZE, 1,
			[revalidate_disk_size() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
