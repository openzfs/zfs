dnl #
dnl # Check for libdevmapper.  libdevmapper is optional for building, but
dnl # required for auto-online/auto-replace functionality for DM/multipath
dnl # disks.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBDEVMAPPER], [
        AC_CHECK_HEADER([libdevmapper.h], [
            AC_SUBST([LIBDEVMAPPER], ["-ldevmapper"])
            AC_DEFINE([HAVE_LIBDEVMAPPER], 1, [Define if you have libdevmapper])

	    user_libdevmapper=yes
        ], [
	    user_libdevmapper=no
	])
])
