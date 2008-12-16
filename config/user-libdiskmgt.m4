dnl #
dnl # Check for libdiskmgt
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBDISKMGT], [
	AC_CHECK_LIB([diskmgt], [libdiskmgt_error],
		[AC_DEFINE([HAVE_LIBDISKMGT], 1,
		[Define to 1 if 'libdiskmgt' library available])])
])
