dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Check for libaio - only used for mmap_libaio test cases.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBAIO], [
	AC_ARG_WITH([libaio],
	    AS_HELP_STRING([--without-libaio], [skip the libaio test helper]),
	    [], [with_libaio=auto])

	user_libaio=no
	AS_IF([test "x$with_libaio" != "xno"], [
		ZFS_AC_FIND_SYSTEM_LIBRARY(LIBAIO, [], [libaio.h], [], [aio], [],
		    [user_libaio=yes], [
			AS_IF([test "x$with_libaio" = "xyes"], [
				AC_MSG_FAILURE([--with-libaio was given, but libaio is not available])
			])
		])
	])
])
