dnl #
dnl # 5.17 API change,
dnl #
dnl # GENHD_FL_EXT_DEVT flag removed
dnl # GENHD_FL_NO_PART_SCAN renamed GENHD_FL_NO_PART
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENHD_FLAGS], [

	ZFS_LINUX_TEST_SRC([genhd_fl_ext_devt], [
		#include <linux/blkdev.h>
	], [
		int flags __attribute__ ((unused)) = GENHD_FL_EXT_DEVT;
	])

	ZFS_LINUX_TEST_SRC([genhd_fl_no_part], [
		#include <linux/blkdev.h>
	], [
		int flags __attribute__ ((unused)) = GENHD_FL_NO_PART;
	])

	ZFS_LINUX_TEST_SRC([genhd_fl_no_part_scan], [
		#include <linux/blkdev.h>
	], [
		int flags __attribute__ ((unused)) = GENHD_FL_NO_PART_SCAN;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENHD_FLAGS], [

	AC_MSG_CHECKING([whether GENHD_FL_EXT_DEVT flag is available])
	ZFS_LINUX_TEST_RESULT([genhd_fl_ext_devt], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(ZFS_GENHD_FL_EXT_DEVT, GENHD_FL_EXT_DEVT,
		    [GENHD_FL_EXT_DEVT flag is available])
	], [
		AC_MSG_RESULT(no)
		AC_DEFINE(ZFS_GENHD_FL_EXT_DEVT, 0,
		    [GENHD_FL_EXT_DEVT flag is not available])
	])

	AC_MSG_CHECKING([whether GENHD_FL_NO_PART flag is available])
	ZFS_LINUX_TEST_RESULT([genhd_fl_no_part], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(ZFS_GENHD_FL_NO_PART, GENHD_FL_NO_PART,
		    [GENHD_FL_NO_PART flag is available])
	], [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether GENHD_FL_NO_PART_SCAN flag is available])
		ZFS_LINUX_TEST_RESULT([genhd_fl_no_part_scan], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(ZFS_GENHD_FL_NO_PART, GENHD_FL_NO_PART_SCAN,
			    [GENHD_FL_NO_PART_SCAN flag is available])
		], [
			ZFS_LINUX_TEST_ERROR([GENHD_FL_NO_PART|GENHD_FL_NO_PART_SCAN])
		])
	])
])
