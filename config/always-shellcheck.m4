dnl #
dnl # Check if shellcheck and/or checkbashisms are available.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_SHELLCHECK], [
	AC_CHECK_PROG([SHELLCHECK], [shellcheck], [yes])
	AC_CHECK_PROG([CHECKBASHISMS], [checkbashisms], [yes])

	AM_CONDITIONAL([HAVE_SHELLCHECK], [test "x$SHELLCHECK" = "xyes"])
	AM_CONDITIONAL([HAVE_CHECKBASHISMS], [test "x$CHECKBASHISMS" = "xyes"])
])
