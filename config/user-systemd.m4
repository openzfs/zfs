AC_DEFUN([ZFS_AC_CONFIG_USER_SYSTEMD], [
	AC_ARG_WITH(systemddir,
		AC_HELP_STRING([--with-systemddir=DIR],
		[install systemd helpers [[EPREFIX/lib/systemd]]]),
		systemddir=$withval, systemddir='${exec_prefix}/lib/systemd')

        AC_ARG_WITH(systemdgeneratordir,
                AC_HELP_STRING([--with-systemdgeneratordir=DIR],
                [install systemd generators [[SYSTEMDDIR/system-generators]]]),
                systemdgeneratordir=$withval, systemdgeneratordir='${systemddir}/system-generators')

	AC_SUBST(systemddir)
	AC_SUBST(systemdgeneratordir)
])
