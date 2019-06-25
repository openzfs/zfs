dnl #
dnl # Check for libmount.  libmount is a part of util-linux source code,
dnl # but requires libmount-devel (or similar name) for <libmount/libmount.h>
dnl # on distros.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBMOUNT], [
	LIBMOUNT=

	AC_CHECK_HEADER([libmount/libmount.h], [
	    AC_SUBST([LIBMOUNT], ["-lmount"])
	    AC_DEFINE([HAVE_LIBMOUNT], 1, [Define if you have libmount])
	], [])
])
