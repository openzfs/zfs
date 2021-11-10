dnl #
dnl # Check if librt is required for clock_gettime.
dnl # clock_gettime is generally available in libc on modern systems.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_CLOCK_GETTIME], [
	AC_CHECK_FUNC([clock_gettime], [], [
	    AC_CHECK_LIB([rt], [clock_gettime], [
		AC_SUBST([LIBCLOCK_GETTIME], [-lrt])], [
		AC_MSG_FAILURE([*** clock_gettime is missing in libc and librt])
	    ])
	])
])
