dnl #
dnl # 4.9 API change
dnl # group_info changed from 2d array via >blocks to 1d array via ->gid
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GROUP_INFO_NBLOCKS], [
	ZFS_LINUX_TEST_SRC([group_info_nblocks], [
		#include <linux/cred.h>
	],[
		struct group_info gi;
		gi.nblocks = 0;
		(void)gi;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GROUP_INFO_NBLOCKS], [
	AC_MSG_CHECKING([whether group_info->nblocks exists])
	ZFS_LINUX_TEST_RESULT([group_info_nblocks], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GROUP_INFO_NBLOCKS, 1, [group_info->nblocks exists])
	],[
		AC_MSG_RESULT(no)
	])
])
