dnl #
dnl # 4.7 API change
dnl # i_mutex is changed to i_rwsem. Instead of directly using
dnl # i_mutex/i_rwsem, we should use inode_lock() and inode_lock_shared()
dnl # We test inode_lock_shared because inode_lock is introduced earlier.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_LOCK], [
	ZFS_LINUX_TEST_SRC([inode_lock], [
		#include <linux/fs.h>
	],[
		struct inode *inode = NULL;
		inode_lock_shared(inode);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_LOCK], [
	AC_MSG_CHECKING([whether inode_lock_shared() exists])
	ZFS_LINUX_TEST_RESULT([inode_lock], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_LOCK_SHARED, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])
