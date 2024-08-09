dnl # Adds --with-zia=PATH to configuration options
dnl # The path provided should point to the DPUSM
dnl # root and contain Module.symvers.
AC_DEFUN([ZFS_AC_ZIA], [
	AC_ARG_WITH([zia],
		AS_HELP_STRING([--with-zia=PATH],
			[Path to Data Processing Services Module]),
		[
			DPUSM_ROOT="$withval"
			AS_IF([test "x$DPUSM_ROOT" != "xno"],
				[enable_zia=yes],
				[enable_zia=no])
		],
		[enable_zia=no]
	)

	AS_IF([test "x$enable_zia" == "xyes"],
		AS_IF([! test -d "$DPUSM_ROOT"],
			[AC_MSG_ERROR([--with-zia=PATH requires the DPUSM root directory])]
		)

		DPUSM_SYMBOLS="$DPUSM_ROOT/Module.symvers"

		AS_IF([test -r $DPUSM_SYMBOLS],
			[
				AC_MSG_RESULT([$DPUSM_SYMBOLS])
				ZIA_CPPFLAGS="-DZIA=1 -I$DPUSM_ROOT/include"
				KERNEL_ZIA_CPPFLAGS="-DZIA=1 -I$DPUSM_ROOT/include"
				WITH_ZIA="_with_zia"

				AC_SUBST(WITH_ZIA)
				AC_SUBST(KERNEL_ZIA_CPPFLAGS)
				AC_SUBST(ZIA_CPPFLAGS)
				AC_SUBST(DPUSM_SYMBOLS)
				AC_SUBST(DPUSM_ROOT)
			],
			[
				AC_MSG_ERROR([
	*** Failed to find Module.symvers in:
	$DPUSM_SYMBOLS
				])
			]
		)
	)
])
