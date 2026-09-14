dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 5.19 API change,
dnl # kiocb->ki_complete() reduced from 3 args to 2:
dnl #   old: void (*ki_complete)(struct kiocb *, long, long)
dnl #   new: void (*ki_complete)(struct kiocb *, long)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KIOCB_KI_COMPLETE], [
	ZFS_LINUX_TEST_SRC([kiocb_ki_complete_2args], [
		#include <linux/fs.h>
	],[
		struct kiocb *kiocb = NULL;
		kiocb->ki_complete(kiocb, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KIOCB_KI_COMPLETE], [
	AC_MSG_CHECKING([whether kiocb->ki_complete() wants 2 args])
	ZFS_LINUX_TEST_RESULT([kiocb_ki_complete_2args], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_KI_COMPLETE, 1,
		    [kiocb->ki_complete() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])
