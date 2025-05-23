dnl #
dnl # Linux 6.0 gets rid of address_space_operations.migratepage
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_MIGRATEPAGE], [
	ZFS_LINUX_TEST_SRC([vfs_has_migratepage], [
		#include <linux/fs.h>
		#include <linux/migrate.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.migratepage	= migrate_page,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_MIGRATEPAGE], [
	dnl #
	dnl # Linux 6.0 gets rid of address_space_operations.migratepage
	dnl #
	AC_MSG_CHECKING([whether migratepage exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_migratepage], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_MIGRATEPAGE, 1, [migratepage exists])
	],[
		AC_MSG_RESULT([no])
	])
])
