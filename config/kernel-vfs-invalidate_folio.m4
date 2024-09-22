dnl #
dnl # Linux 5.18 uses invalidate_folio in lieu of invalidate_page
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_INVALIDATE_FOLIO], [
	ZFS_LINUX_TEST_SRC([vfs_has_invalidate_folio], [
		#include <linux/fs.h>

		static void
		test_invalidate_folio(struct folio *folio, size_t offset,
		                      size_t len) {
			(void) folio; (void) offset; (void) len;
			return;
		}

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.invalidate_folio	= test_invalidate_folio,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_INVALIDATE_FOLIO], [
	dnl #
	dnl # Linux 5.18 uses invalidate_folio in lieu of invalidate_page
	dnl #
	AC_MSG_CHECKING([whether invalidate_folio exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_invalidate_folio], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_INVALIDATE_FOLIO, 1, [invalidate_folio exists])
	],[
		AC_MSG_RESULT([no])
	])
])
