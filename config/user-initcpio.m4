AC_DEFUN([ZFS_AC_CONFIG_USER_INITCPIO], [
	AC_ARG_WITH(initcpio_prefix,
		AC_HELP_STRING([--with-initcpio=INITCPIODIR],
			[install initcpio files to INITCPIODIR [[EPREFIX/lib/initcpio]]]),
		initcpiodir=$withval, initcpiodir='${exec_prefix}/lib/initcpio')

	AC_ARG_ENABLE([initcpio],
 		AS_HELP_STRING([--enable-initcpio], [enable installing initcpio files for use with mkinitcpio]),
 			[enable_initcpio="$enableval"],
			[enable_initcpio="no"])

	AC_SUBST(initcpiodir)
    AM_CONDITIONAL(SUPPORT_INITCPIO, test "x$enable_initcpio" != "xno")
])
