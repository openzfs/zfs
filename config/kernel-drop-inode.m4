dnl #
dnl # 6.18 API change
dnl # - generic_drop_inode() renamed to inode_generic_drop()
dnl # - generic_delete_inode() renamed to inode_just_drop()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_GENERIC_DROP], [
	ZFS_LINUX_TEST_SRC([inode_generic_drop], [
		#include <linux/fs.h>
	],[
		struct inode *ip = NULL;
		inode_generic_drop(ip);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_GENERIC_DROP], [
	AC_MSG_CHECKING([whether inode_generic_drop() exists])
	ZFS_LINUX_TEST_RESULT([inode_generic_drop], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_GENERIC_DROP, 1,
			[inode_generic_drop() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
