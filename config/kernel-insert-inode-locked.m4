dnl #
dnl # 2.6.28 API change
dnl # Added insert_inode_locked() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INSERT_INODE_LOCKED], [
	ZFS_LINUX_TEST_SRC([insert_inode_locked], [
		#include <linux/fs.h>
	], [
		insert_inode_locked(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INSERT_INODE_LOCKED], [
	AC_MSG_CHECKING([whether insert_inode_locked() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([insert_inode_locked],
	    [insert_inode_locked], [fs/inode.c], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([insert_inode_locked()])
	])
])
