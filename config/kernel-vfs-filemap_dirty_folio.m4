dnl #
dnl # Linux 5.18 uses filemap_dirty_folio in lieu of
dnl # ___set_page_dirty_nobuffers
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_FILEMAP_DIRTY_FOLIO], [
	ZFS_LINUX_TEST_SRC([vfs_has_filemap_dirty_folio], [
		#include <linux/pagemap.h>
		#include <linux/writeback.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.dirty_folio	= filemap_dirty_folio,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_FILEMAP_DIRTY_FOLIO], [
	dnl #
	dnl # Linux 5.18 uses filemap_dirty_folio in lieu of
	dnl # ___set_page_dirty_nobuffers
	dnl #
	AC_MSG_CHECKING([whether filemap_dirty_folio exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_filemap_dirty_folio], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_FILEMAP_DIRTY_FOLIO, 1,
			[filemap_dirty_folio exists])
	],[
		AC_MSG_RESULT([no])
	])
])
