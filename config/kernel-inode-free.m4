AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_FREE], [
	ZFS_LINUX_TEST_SRC([inode_free], [
		#include <linux/fs.h>

		static void inode_free(struct inode *ip)
		{ return; }

		static const struct super_operations
			iops __attribute__ ((unused)) = {
			.free_inode = inode_free,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_FREE], [
	AC_MSG_CHECKING([whether inode_free() is available])
	ZFS_LINUX_TEST_RESULT([inode_free], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_FREE, 1,
		    [.inode_free() i_op exists])
       ],[
		AC_MSG_RESULT(no)
       ])
])

