dnl #
dnl # Check if shellcheck is available.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_SHELLCHECK], [
	AC_CHECK_PROG([SHELLCHECK], [shellcheck], [yes])
	AM_CONDITIONAL([HAVE_SHELLCHECK], [test "x$SHELLCHECK" = "xyes"])
])
