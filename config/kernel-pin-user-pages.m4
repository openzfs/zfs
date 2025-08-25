dnl #
dnl # Check for pin_user_pages_unlocked().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PIN_USER_PAGES], [
	ZFS_LINUX_TEST_SRC([pin_user_pages_unlocked], [
		#include <linux/mm.h>
	],[
		unsigned long start = 0;
		unsigned long nr_pages = 1;
		struct page **pages = NULL;
		unsigned int gup_flags = 0;
		long ret __attribute__ ((unused));

		ret = pin_user_pages_unlocked(start, nr_pages, pages,
		    gup_flags);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PIN_USER_PAGES], [

	dnl #
	dnl # Kernal 5.8 introduced the pin_user_pages* interfaces which should
	dnl # be used for Direct I/O requests.
	dnl #
	AC_MSG_CHECKING([whether pin_user_pages_unlocked() is available])
	ZFS_LINUX_TEST_RESULT([pin_user_pages_unlocked], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PIN_USER_PAGES_UNLOCKED, 1,
		    [pin_user_pages_unlocked() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
