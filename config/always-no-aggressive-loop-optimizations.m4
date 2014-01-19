dnl #
dnl # Check if gcc supports -fno-aggressive-loop-optimizations
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_NO_AGGRESSIVE_LOOP_OPTIMIZATIONS], [
	AC_MSG_CHECKING([for -fno-aggressive-loop-optimizations support])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -fno-aggressive-loop-optimizations"

	AC_RUN_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_AGGRESSIVE_LOOP_OPTIMIZATIONS=-fno-aggressive-loop-optimizations
		AC_MSG_RESULT([yes])
	], [
		NO_AGGRESSIVE_LOOP_OPTIMIZATIONS=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_AGGRESSIVE_LOOP_OPTIMIZATIONS])
])
