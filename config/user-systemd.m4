AC_DEFUN([ZFS_AC_CONFIG_USER_SYSTEMD], [
	AC_ARG_WITH(systemddir,
		AC_HELP_STRING([--with-systemddir=DIR],
		[install systemd unit files [[EPREFIX/lib/systemd/system]]]),
		systemddir=$withval, systemddir='${exec_prefix}/lib/systemd/system')
	AC_ARG_WITH(systemdpresetdir,
		AC_HELP_STRING([--with-systemdpresetdir=DIR],
		[install systemd unit files [[EPREFIX/lib/systemd/system-preset]]]),
		systemdpresetdir=$withval, systemdpresetdir='${exec_prefix}/lib/systemd/system-preset')

	AC_SUBST(systemddir)
	AC_SUBST(systemdpresetdir)
])
