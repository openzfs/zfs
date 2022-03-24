dnl #
dnl # 5.11 API change
dnl # revalidate_disk_size() has been removed entirely.
dnl #
dnl # 5.10 API change
dnl # revalidate_disk() was replaced by revalidate_disk_size()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_REVALIDATE_DISK], [

	ZFS_LINUX_TEST_SRC([revalidate_disk_size], [
		#include <linux/blkdev.h>
	], [
		struct gendisk *disk = NULL;
		(void) revalidate_disk_size(disk, false);
	])

	ZFS_LINUX_TEST_SRC([revalidate_disk], [
		#include <linux/blkdev.h>
	], [
		struct gendisk *disk = NULL;
		(void) revalidate_disk(disk);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_REVALIDATE_DISK], [

	AC_MSG_CHECKING([whether revalidate_disk_size() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([revalidate_disk_size],
		[revalidate_disk_size], [block/genhd.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REVALIDATE_DISK_SIZE, 1,
		    [revalidate_disk_size() is available])
	], [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether revalidate_disk() is available])
		ZFS_LINUX_TEST_RESULT_SYMBOL([revalidate_disk],
		    [revalidate_disk], [block/genhd.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_REVALIDATE_DISK, 1,
			    [revalidate_disk() is available])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
