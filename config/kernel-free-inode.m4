dnl #
dnl # Linux 5.2 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SOPS_FREE_INODE], [
	ZFS_LINUX_TEST_SRC([super_operations_free_inode], [
		#include <linux/fs.h>

		static void free_inode(struct inode *) { }

		static struct super_operations sops __attribute__ ((unused)) = {
			.free_inode = free_inode,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SOPS_FREE_INODE], [
	AC_MSG_CHECKING([whether sops->free_inode() exists])
	ZFS_LINUX_TEST_RESULT([super_operations_free_inode], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SOPS_FREE_INODE, 1, [sops->free_inode() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
