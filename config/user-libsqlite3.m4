dnl #
dnl # Check for libsqlite3 - optional dependency for zmetad
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBSQLITE3], [
	AC_ARG_ENABLE([zmetad],
		AS_HELP_STRING([--enable-zmetad],
			[Build zmetad metadata export daemon @<:@default=check@:>@]),
		[enable_zmetad=$enableval],
		[enable_zmetad=check])

	HAVE_LIBSQLITE3=0
	AS_IF([test "x$enable_zmetad" != xno], [
		ZFS_AC_FIND_SYSTEM_LIBRARY(LIBSQLITE3, [sqlite3], [sqlite3.h], [], [sqlite3], [sqlite3_open sqlite3_close], [
			HAVE_LIBSQLITE3=1
			AC_DEFINE(HAVE_LIBSQLITE3, 1, [Define if you have libsqlite3])
		], [
			AS_IF([test "x$enable_zmetad" = xyes], [
				AC_MSG_FAILURE([*** libsqlite3-dev package required for zmetad])
			])
		])
	])

	AC_SUBST(HAVE_LIBSQLITE3)
])
