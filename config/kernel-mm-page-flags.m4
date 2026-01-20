dnl # SPDX-License-Identifier: CDDL-1.0
AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_FLAG_ERROR], [
	ZFS_LINUX_TEST_SRC([mm_page_flag_error], [
		#include <linux/page-flags.h>

		static enum pageflags
		    test_flag __attribute__((unused)) = PG_error;
	])
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_FLAG_ERROR], [
	AC_MSG_CHECKING([whether PG_error flag is available])
	ZFS_LINUX_TEST_RESULT([mm_page_flag_error], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MM_PAGE_FLAG_ERROR, 1, [PG_error flag is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 6.18+ uses a struct typedef (memdesc_flags_t) instead of an
dnl # 'unsigned long' for the 'flags' field in 'struct page'.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_FLAGS_STRUCT], [
	ZFS_LINUX_TEST_SRC([mm_page_flags_struct], [
		#include <linux/mm.h>

		static const struct page p __attribute__ ((unused)) = {
			.flags = { .f = 0 }
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_FLAGS_STRUCT], [
	AC_MSG_CHECKING([whether 'flags' in 'struct page' is a struct])
	ZFS_LINUX_TEST_RESULT([mm_page_flags_struct], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_MM_PAGE_FLAGS_STRUCT, 1,
			['flags' in 'struct page' is a struct])
	],[
		AC_MSG_RESULT([no])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_FLAGS], [
	ZFS_AC_KERNEL_SRC_MM_PAGE_FLAG_ERROR
	ZFS_AC_KERNEL_SRC_MM_PAGE_FLAGS_STRUCT
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_FLAGS], [
	ZFS_AC_KERNEL_MM_PAGE_FLAG_ERROR
	ZFS_AC_KERNEL_MM_PAGE_FLAGS_STRUCT
])
