dnl #
dnl # 5.14 API change
dnl # blk_alloc_queue deprecated in favor of blk_alloc_disk
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLK_ALLOC_DISK], [
	ZFS_LINUX_TEST_SRC([blk_alloc_disk], [
		#include <linux/genhd.h>
	], [
		struct gendisk *disk = NULL;
		disk = blk_alloc_disk(0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_ALLOC_DISK], [
	AC_MSG_CHECKING([whether blk_alloc_disk() is available])
	ZFS_LINUX_TEST_RESULT([blk_alloc_disk], [
		    AC_MSG_RESULT(yes)
		    AC_DEFINE(HAVE_BLK_ALLOC_DISK, 1,
			    [blk_alloc_disk() is available])
		], [
			AC_MSG_RESULT(no)
	])
])
