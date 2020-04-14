dnl #
dnl # Check for setproctitle
dnl # In BSD, this is in libc; in Linux, it tends to be in -lbsd
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_SETPROCTITLE], [
	AC_SEARCH_LIBS([setproctitle], [bsd], [
		AC_DEFINE(HAVE_PROCTITLE, 1,
			[Define if the system has setproctitle])
		]
	)
	AC_CHECK_HEADER([bsd/bsd.h], [
	    AC_DEFINE([HAVE_LIBBSD], 1, [Define if you have libbsd])
	])
])
