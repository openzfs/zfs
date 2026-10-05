dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Determine whether to build the ZetaWatch macOS menu bar application
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_ZETAWATCH], [
	AC_ARG_ENABLE(zetawatch,
		AS_HELP_STRING([--enable-zetawatch],
		[Build the ZetaWatch macOS menu bar application @<:@default=no@:>@]),
		[enable_zetawatch=$enableval],
		[enable_zetawatch=no])

	AM_CONDITIONAL([ENABLE_ZETAWATCH], [test "x$enable_zetawatch" = "xyes"])
	AC_MSG_CHECKING(for ZetaWatch support)
	AC_MSG_RESULT([$enable_zetawatch])

	dnl # ibtool/actool (Interface Builder nib and asset catalog compilers)
	dnl # ship only with full Xcode.app, never the standalone Command Line
	dnl # Tools. When absent, ZetaWatch's Makefile.am falls back to the
	dnl # pre-compiled .nib/Assets.car files committed alongside the .xib/
	dnl # .xcassets sources, so the app can still be built - just not
	dnl # redesigned - on a CLT-only machine.
	AS_IF([test "x$enable_zetawatch" = "xyes"], [
		AC_MSG_CHECKING([for ibtool])
		IBTOOL=`xcrun -f ibtool 2>/dev/null`
		AS_IF([test -n "$IBTOOL"],
			[AC_MSG_RESULT([$IBTOOL])],
			[AC_MSG_RESULT([not found, will use committed pre-compiled .nib files])])
		AC_SUBST([IBTOOL])

		AC_MSG_CHECKING([for actool])
		ACTOOL=`xcrun -f actool 2>/dev/null`
		AS_IF([test -n "$ACTOOL"],
			[AC_MSG_RESULT([$ACTOOL])],
			[AC_MSG_RESULT([not found, will use committed pre-compiled Assets.car])])
		AC_SUBST([ACTOOL])
	])
])
