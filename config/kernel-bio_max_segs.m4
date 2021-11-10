dnl #
dnl # 5.12 API change removes BIO_MAX_PAGES in favor of bio_max_segs()
dnl # which will handle the logic of setting the upper-bound to a
dnl # BIO_MAX_PAGES, internally.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_MAX_SEGS], [
	ZFS_LINUX_TEST_SRC([bio_max_segs], [
		#include <linux/bio.h>
	],[
		bio_max_segs(1);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_MAX_SEGS], [
	AC_MSG_CHECKING([whether bio_max_segs() exists])
	ZFS_LINUX_TEST_RESULT([bio_max_segs], [
		AC_MSG_RESULT(yes)

		AC_DEFINE([HAVE_BIO_MAX_SEGS], 1, [bio_max_segs() is implemented])
	],[
		AC_MSG_RESULT(no)
	])
])
