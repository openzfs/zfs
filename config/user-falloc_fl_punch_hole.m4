dnl #
dnl # Check if the libc supports file hole punching.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_FALLOC_FL_PUNCH_HOLE], [
	AC_MSG_CHECKING([whether FALLOC_FL_PUNCH_HOLE is defined in userspace])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
	[
		#include <linux/falloc.h>
	],
	[
		#ifndef FALLOC_FL_PUNCH_HOLE
		#error FALLOC_FL_PUNCH_HOLE is undefined
		#endif
	])],
	[
		AC_DEFINE([HAVE_USER_FALLOC_FL_PUNCH_HOLE], 1,
			[Define to 1 if the userspace provides FALLOC_FL_PUNCH_HOLE])
		AC_MSG_RESULT([yes])
	],
	[
		AC_MSG_RESULT([no])
	])
])
