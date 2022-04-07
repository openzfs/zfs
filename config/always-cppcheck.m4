dnl #
dnl # Check if cppcheck is available.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CPPCHECK], [
	AC_CHECK_PROG([CPPCHECK], [cppcheck], [cppcheck])
	if test -z "$CPPCHECK"; then
		CPPCHECK='@printf "skipping cppcheck because cppcheck is not installed\n"'
	fi
])
