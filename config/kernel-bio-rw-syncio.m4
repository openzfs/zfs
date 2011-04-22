dnl #
dnl # Preferred interface for flagging a synchronous bio:
dnl # 2.6.12-2.6.29: BIO_RW_SYNC
dnl # 2.6.30-2.6.35: BIO_RW_SYNCIO
dnl # 2.6.36-2.6.xx: REQ_SYNC
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_RW_SYNC], [
	AC_MSG_CHECKING([whether BIO_RW_SYNC is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = BIO_RW_SYNC;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_RW_SYNC, 1, [BIO_RW_SYNC is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_RW_SYNCIO], [
	AC_MSG_CHECKING([whether BIO_RW_SYNCIO is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = BIO_RW_SYNCIO;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_RW_SYNCIO, 1, [BIO_RW_SYNCIO is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_REQ_SYNC], [
	AC_MSG_CHECKING([whether REQ_SYNC is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = REQ_SYNC;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_SYNC, 1, [REQ_SYNC is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
