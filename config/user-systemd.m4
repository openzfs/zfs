AC_DEFUN([ZFS_AC_CONFIG_USER_SYSTEMD], [
	AC_ARG_WITH(systemddir,
		AC_HELP_STRING([--with-systemddir=DIR],
		[install systemd helpers [[EPREFIX/lib/systemd]]]),
		systemddir=$withval, systemddir='${exec_prefix}/lib/systemd')

	AC_SUBST(systemddir)
])
