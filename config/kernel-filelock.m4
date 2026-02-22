dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 6.3 API change
dnl # locking support functions (eg generic_setlease) were moved out of
dnl # linux/fs.h to linux/filelock.h
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILELOCK_HEADER], [
	ZFS_LINUX_TEST_SRC([filelock_header], [
		#include <linux/fs.h>
		#include <linux/filelock.h>
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_FILELOCK_HEADER], [
	AC_MSG_CHECKING([for standalone filelock header])
	ZFS_LINUX_TEST_RESULT([filelock_header], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILELOCK_HEADER, 1, [linux/filelock.h exists])
	], [
		AC_MSG_RESULT(no)
	])
])

