dnl #
dnl # 5.0 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TOTALHIGH_PAGES], [
	ZFS_LINUX_TEST_SRC([totalhigh_pages], [
		#include <linux/highmem.h>
	],[
		unsigned long pages __attribute__ ((unused));
		pages = totalhigh_pages();
	])
])

AC_DEFUN([ZFS_AC_KERNEL_TOTALHIGH_PAGES], [
	AC_MSG_CHECKING([whether totalhigh_pages() exists])
	ZFS_LINUX_TEST_RESULT([totalhigh_pages], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TOTALHIGH_PAGES, 1, [totalhigh_pages() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
