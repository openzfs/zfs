dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Determine whether to enable zfs channel program support
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_ZCP], [
	AC_ARG_ENABLE(zcp,
		AS_HELP_STRING([--enable-zcp],
		[Enable zfs channel program support @<:@default=yes@:>@]),
		[enable_zcp=$enableval],
		[enable_zcp=yes])


	AS_IF([test "x$enable_zcp" = xno], [
		AC_DEFINE([DISABLE_ZCP], [1],
		[Define to 1 to disable ZFS channel program support])
	])

	AM_CONDITIONAL([ZCP_ENABLED], [test "x$enable_zcp" = "xyes"])
	AC_SUBST([ZCP_ENABLED], [$enable_zcp])
	AC_MSG_CHECKING(for zfs channel program support)
	AC_MSG_RESULT([$enable_zcp])
])
