dnl #
dnl # Linux 5.18 removes address_space_operations ->readpages in favour of
dnl # ->readahead
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_READPAGES], [
	ZFS_LINUX_TEST_SRC([vfs_has_readpages], [
		#include <linux/fs.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.readpages = NULL,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_READPAGES], [
	AC_MSG_CHECKING([whether aops->readpages exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_readpages], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_READPAGES, 1,
			[address_space_operations->readpages exists])
	],[
		AC_MSG_RESULT([no])
	])
])
