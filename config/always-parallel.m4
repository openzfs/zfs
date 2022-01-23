dnl #
dnl # Check if GNU parallel is available.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_PARALLEL], [
	AC_CHECK_PROG([PARALLEL], [parallel], [yes])

	AM_CONDITIONAL([HAVE_PARALLEL], [test "x$PARALLEL" = "xyes"])
])
