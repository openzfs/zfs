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

AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_FLAGS], [
	ZFS_AC_KERNEL_SRC_MM_PAGE_FLAG_ERROR
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_FLAGS], [
	ZFS_AC_KERNEL_MM_PAGE_FLAG_ERROR
])
