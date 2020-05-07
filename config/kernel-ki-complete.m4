dnl#
dnl# Check for ki_complete field
dnl#
AC_DEFUN([ZFS_AC_KERNEL_SRC_KI_COMPLETE], [
	ZFS_LINUX_TEST_SRC([ki_complete_exists], [
		#include <linux/fs.h>
	], [
		struct kiocb iocb;
		iocb.ki_complete = NULL;
	])
])

dnl #
dnl # Supported get_user_pages/_unlocked interfaces checked newest to oldest.
dnl # We first check for get_user_pages_unlocked as that is available in
dnl # newer kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_KI_COMPLETE], [
	dnl #
	dnl # Current API of get_user_pages_unlocked
	dnl #
	AC_MSG_CHECKING([whether ki_complete field exists])
	ZFS_LINUX_TEST_RESULT([ki_complete_exists], [
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_KI_COMPLETE, 1,
		    [ki_complete field exists])
	])
])
