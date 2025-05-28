dnl #
dnl # Linux 6.16 removes address_space_operations ->writepage
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_WRITEPAGE], [
	ZFS_LINUX_TEST_SRC([vfs_has_writepage], [
		#include <linux/fs.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.writepage = NULL,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_WRITEPAGE], [
	AC_MSG_CHECKING([whether aops->writepage exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_writepage], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_WRITEPAGE, 1,
			[address_space_operations->writepage exists])
	],[
		AC_MSG_RESULT([no])
	])
])
