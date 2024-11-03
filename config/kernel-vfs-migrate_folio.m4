dnl #
dnl # Linux 6.0 uses migrate_folio in lieu of migrate_page
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_MIGRATE_FOLIO], [
	ZFS_LINUX_TEST_SRC([vfs_has_migrate_folio], [
		#include <linux/fs.h>
		#include <linux/migrate.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.migrate_folio	= migrate_folio,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_MIGRATE_FOLIO], [
	dnl #
	dnl # Linux 6.0 uses migrate_folio in lieu of migrate_page
	dnl #
	AC_MSG_CHECKING([whether migrate_folio exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_migrate_folio], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_MIGRATE_FOLIO, 1, [migrate_folio exists])
	],[
		AC_MSG_RESULT([no])
	])
])
