dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Linux 6.8 replaced generic_error_remove_page() with
dnl # generic_error_remove_folio().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_ERROR_REMOVE_FOLIO], [
	ZFS_LINUX_TEST_SRC([generic_error_remove_folio], [
		#include <linux/mm.h>
		#include <linux/fs.h>
	],[
		struct address_space *mapping __attribute__ ((unused)) = NULL;
		struct folio *folio __attribute__ ((unused)) = NULL;

		(void) generic_error_remove_folio(mapping, folio);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_ERROR_REMOVE_FOLIO], [
	AC_MSG_CHECKING([whether generic_error_remove_folio() exists])
	ZFS_LINUX_TEST_RESULT([generic_error_remove_folio], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_GENERIC_ERROR_REMOVE_FOLIO, 1,
			[generic_error_remove_folio() exists])
	],[
		AC_MSG_RESULT([no])
	])
])
