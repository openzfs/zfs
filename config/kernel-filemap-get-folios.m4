dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Linux 6.2 added filemap_get_folios() to look up the cached folios of a
dnl # mapping, and later removed find_get_pages_range().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILEMAP_GET_FOLIOS], [
	ZFS_LINUX_TEST_SRC([filemap_get_folios], [
		#include <linux/pagemap.h>
	],[
		struct address_space *mapping __attribute__ ((unused)) = NULL;
		struct folio_batch *fbatch __attribute__ ((unused)) = NULL;
		pgoff_t start __attribute__ ((unused)) = 0;

		(void) filemap_get_folios(mapping, &start, (pgoff_t)-1,
		    fbatch);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FILEMAP_GET_FOLIOS], [
	AC_MSG_CHECKING([whether filemap_get_folios() exists])
	ZFS_LINUX_TEST_RESULT([filemap_get_folios], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_FILEMAP_GET_FOLIOS, 1,
			[filemap_get_folios() exists])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Linux 7.1 moved struct folio_batch from linux/pagevec.h, which was
dnl # removed, to linux/folio_batch.h.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FOLIO_BATCH_HEADER], [
	ZFS_LINUX_TEST_SRC([folio_batch_header], [
		#include <linux/fs.h>
		#include <linux/folio_batch.h>
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_FOLIO_BATCH_HEADER], [
	AC_MSG_CHECKING([for standalone folio_batch header])
	ZFS_LINUX_TEST_RESULT([folio_batch_header], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FOLIO_BATCH_HEADER, 1,
			[linux/folio_batch.h exists])
	], [
		AC_MSG_RESULT(no)
	])
])
