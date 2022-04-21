dnl #
dnl # 4.9 API change
dnl # group_info changed from 2d array via >blocks to 1d array via ->gid
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GROUP_INFO_GID], [
	ZFS_LINUX_TEST_SRC([group_info_gid], [
		#include <linux/cred.h>
	],[
		struct group_info gi __attribute__ ((unused)) = {};
		gi.gid[0] = KGIDT_INIT(0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GROUP_INFO_GID], [
	AC_MSG_CHECKING([whether group_info->gid exists])
	ZFS_LINUX_TEST_RESULT([group_info_gid], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GROUP_INFO_GID, 1, [group_info->gid exists])
	],[
		AC_MSG_RESULT(no)
	])
])
