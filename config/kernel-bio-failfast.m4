dnl #
dnl # 2.6.28 API change
dnl # Device, transport, and driver FAILFAST flags were added and
dnl # the now legacy BIO_RW_FAILFAST flag was removed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_FAILFAST], [
	AC_MSG_CHECKING([whether BIO_RW_FAILFAST_* are defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags;
		flags = ((1 << BIO_RW_FAILFAST_DEV) |
			 (1 << BIO_RW_FAILFAST_TRANSPORT) |
			 (1 << BIO_RW_FAILFAST_DRIVER));
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_RW_FAILFAST, 1,
		          [BIO_RW_FAILFAST_* are defined])
	],[
		AC_MSG_RESULT(no)
	])
])
