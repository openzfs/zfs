dnl #
dnl # Linux 5.0: totalram_pages is no longer a global variable, and must be
dnl # read via the totalram_pages() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TOTALRAM_PAGES_FUNC], [
	AC_MSG_CHECKING([whether totalram_pages() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		unsigned long pages __attribute__ ((unused));
		pages = totalram_pages();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TOTALRAM_PAGES_FUNC, 1, [kernel has totalram_pages()])
	],[
		AC_MSG_RESULT(no)
	])
])
