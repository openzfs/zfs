AC_DEFUN([ZFS_AC_CONFIG_USER_DRACUT], [
	AC_ARG_WITH(dracutdir,
		AC_HELP_STRING([--with-dracutdir=DIR],
		[install dracut helpers [[EPREFIX/lib/dracut]]]),
		dracutdir=$withval, dracutdir='${exec_prefix}/lib/dracut')

	AC_SUBST(dracutdir)
])
