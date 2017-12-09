dnl #
dnl # Check if gcc supports -fsanitize=address. Applies to all libraries,
dnl # ztest and zdb
dnl #
dnl # LDFLAGS needs -fsanitize=address at all times so libraries compiled with
dnl # it will be linked successfully. CFLAGS will vary by binary being built.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_GCC_FSANITIZE], [
	if test "$enable_debug" = "yes"
	then
		AC_MSG_CHECKING([for -fsanitize=address support])

		saved_cflags="$CFLAGS"
		saved_ldflags="$LDFLAGS"
		CFLAGS="$CFLAGS -fsanitize=address"
		LDFLAGS="$LDFLAGS -fsanitize=address"

		AC_TRY_LINK(
		[
			#include <time.h>
		],[
			time_t now;
			now = time(&now);
		],[
			FSANITIZE_ADDRESS=-fsanitize=address
			saved_ldflags="$saved_ldflags -fsanitize=address"
			AC_MSG_RESULT(yes)
		],[
			FSANITIZE_ADDRESS=""
			AC_MSG_RESULT(no)
		])

		CFLAGS="$saved_flags"
		LDFLAGS="$saved_ldflags"
	fi
	AC_SUBST([FSANITIZE_ADDRESS])
])


dnl #
dnl # Check if gcc supports -Wframe-larger-than=<size> option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_FRAME_LARGER_THAN], [
	AC_MSG_CHECKING([for -Wframe-larger-than=<size> support])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Wframe-larger-than=4096"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
	[
		FRAME_LARGER_THAN=-Wframe-larger-than=4096
		AC_MSG_RESULT([yes])
	],
	[
		FRAME_LARGER_THAN=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([FRAME_LARGER_THAN])
])

