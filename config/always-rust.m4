dnl #
dnl # Determine if rust devtools are installed
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_RUST], [
	AC_MSG_CHECKING([for rust devtools])
	AS_IF([cargo &>/dev/null], [ac_cargo="yes"], [ac_cargo="no"])

	AC_MSG_RESULT([$ac_cargo])
	AM_CONDITIONAL([BUILD_RUST], [test "x$ac_cargo" = "xyes"])
])
