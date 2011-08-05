AC_DEFUN([ZFS_AC_CONFIG_USER_UDEV], [
	AC_ARG_WITH(udevdir,
		AC_HELP_STRING([--with-udevdir=DIR],
		[install udev helpers [[EPREFIX/lib/udev]]]),
		udevdir=$withval, udevdir='${exec_prefix}/lib/udev')

	AC_ARG_WITH(udevruledir,
		AC_HELP_STRING([--with-udevruledir=DIR],
		[install udev rules [[UDEVDIR/rules.d]]]),
		udevruledir=$withval, udevruledir='${udevdir}/rules.d')

	AC_SUBST(udevdir)
	AC_SUBST(udevruledir)
])
