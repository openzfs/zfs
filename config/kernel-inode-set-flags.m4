dnl #
dnl # 3.15 API change
dnl # inode_set_flags introduced to set i_flags
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_SET_FLAGS], [
	ZFS_LINUX_TEST_SRC([inode_set_flags], [
		#include <linux/fs.h>
	],[
		struct inode inode;
		inode_set_flags(&inode, S_IMMUTABLE, S_IMMUTABLE);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_SET_FLAGS], [
	AC_MSG_CHECKING([whether inode_set_flags() exists])
	ZFS_LINUX_TEST_RESULT([inode_set_flags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_SET_FLAGS, 1, [inode_set_flags() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
