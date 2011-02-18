dnl #
dnl # Check to see if the selinux libraries are available.  If they
dnl # are then they will be consulted during mount to determine if
dnl # selinux is enabled or disabled.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBSELINUX], [
	AC_ARG_WITH([selinux],
		[AS_HELP_STRING([--with-selinux],
		[support selinux @<:@default=check@:>@])],
		[],
		[with_selinux=check])

	LIBSELINUX=
	AS_IF([test "x$with_selinux" != xno], [
		AC_CHECK_HEADER([selinux/selinux.h], [
			AC_CHECK_LIB([selinux], [is_selinux_enabled], [
				AC_SUBST([LIBSELINUX], ["-lselinux"])
				AC_DEFINE([HAVE_LIBSELINUX], 1,
					[Define if you have selinux])
			], [
				AS_IF([test "x$with_selinux" != xcheck],
					[AC_MSG_FAILURE(
					[--with-selinux given but unavailable])
				])
			])
		], [
			AS_IF([test "x$with_selinux" != xcheck],
				[AC_MSG_FAILURE(
				[--with-selinux given but unavailable])
			])
		])
	], [
		AC_MSG_CHECKING([for selinux support])
		AC_MSG_RESULT([no])
	])
])
