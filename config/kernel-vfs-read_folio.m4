dnl #
dnl # Linux 5.19 uses read_folio in lieu of readpage
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_READ_FOLIO], [
	ZFS_LINUX_TEST_SRC([vfs_has_read_folio], [
		#include <linux/fs.h>

		static int
		test_read_folio(struct file *file, struct folio *folio) {
			(void) file; (void) folio;
			return (0);
		}

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.read_folio	= test_read_folio,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_READ_FOLIO], [
	dnl #
	dnl # Linux 5.19 uses read_folio in lieu of readpage
	dnl #
	AC_MSG_CHECKING([whether read_folio exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_read_folio], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_READ_FOLIO, 1, [read_folio exists])
	],[
		AC_MSG_RESULT([no])
	])
])
