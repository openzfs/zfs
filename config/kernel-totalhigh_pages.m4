dnl #
dnl # 5.0 API change
dnl #
dnl # ca79b0c211af mm: convert totalram_pages and totalhigh_pages variables to atomic
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TOTALHIGH_PAGES], [
	AC_MSG_CHECKING([whether totalhigh_pages() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/highmem.h>
	],[
		unsigned long pages __attribute__ ((unused));
		pages = totalhigh_pages();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TOTALHIGH_PAGES, 1, [totalhigh_pages() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
