dnl #
dnl # If -latomic exists and atomic.c doesn't link without it,
dnl # it's needed for __atomic intrinsics.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBATOMIC], [
	AC_MSG_CHECKING([whether -latomic is required])

	saved_libs="$LIBS"
	LIBS="$LIBS -latomic"
	LIBATOMIC_LIBS=""

	AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [
		LIBS="$saved_libs"
		saved_cflags="$CFLAGS"
		CFLAGS="$CFLAGS -isystem lib/libspl/include"
		AC_LINK_IFELSE([AC_LANG_PROGRAM([#include "lib/libspl/atomic.c"], [])], [], [LIBATOMIC_LIBS="-latomic"])
		CFLAGS="$saved_cflags"
	])

	if test -n "$LIBATOMIC_LIBS"; then
		AC_MSG_RESULT([yes])
	else
		AC_MSG_RESULT([no])
	fi

	LIBS="$saved_libs"
	AC_SUBST([LIBATOMIC_LIBS])
])
