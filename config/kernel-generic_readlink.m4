dnl #
dnl # 4.10 API
dnl #
dnl # NULL inode_operations.readlink implies generic_readlink(), which
dnl # has been made static.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_READLINK_GLOBAL], [
	ZFS_LINUX_TEST_SRC([generic_readlink_global], [
		#include <linux/fs.h>
	],[
		int i __attribute__ ((unused));
		i = generic_readlink(NULL, NULL, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_READLINK_GLOBAL], [
	AC_MSG_CHECKING([whether generic_readlink is global])
	ZFS_LINUX_TEST_RESULT([generic_readlink_global], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_GENERIC_READLINK, 1,
		    [generic_readlink is global])
	],[
		AC_MSG_RESULT([no])
	])
])
