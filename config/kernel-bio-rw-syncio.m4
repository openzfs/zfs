dnl #
dnl # 2.6.29 API change
dnl # BIO_RW_SYNC renamed to BIO_RW_SYNCIO
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_RW_SYNCIO], [
	AC_MSG_CHECKING([whether BIO_RW_SYNCIO is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags;
		flags = BIO_RW_SYNCIO;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_RW_SYNCIO, 1,
		          [BIO_RW_SYNCIO is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
