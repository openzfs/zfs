dnl #
dnl # ZERO_PAGE() is an alias for emtpy_zero_page. On certain architectures
dnl # this is a GPL exported variable.
dnl #

dnl #
dnl # Checking if ZERO_PAGE is exported GPL-only
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_ZERO_PAGE], [
	ZFS_LINUX_TEST_SRC([zero_page], [
		#include <asm/pgtable.h>
	], [
		struct page *p __attribute__ ((unused));
		p = ZERO_PAGE(0);
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_ZERO_PAGE], [
	AC_MSG_CHECKING([whether ZERO_PAGE() is GPL-only])
	ZFS_LINUX_TEST_RESULT([zero_page_license], [
		AC_MSG_RESULT(no)
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZERO_PAGE_GPL_ONLY, 1,
		    [ZERO_PAGE() is GPL-only])
	])
])
