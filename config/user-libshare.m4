dnl #
dnl # Check for libshare
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBSHARE], [
	AC_CHECK_LIB([share], [sa_init],
		[AC_DEFINE([HAVE_LIBSHARE], 1,
		[Define to 1 if 'libshare' library available])])
])
