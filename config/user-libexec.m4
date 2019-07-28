AC_DEFUN([ZFS_AC_CONFIG_USER_ZFSEXEC], [
	AC_ARG_WITH(zfsexecdir,
		AC_HELP_STRING([--with-zfsexecdir=DIR],
		[install scripts [[@<:@libexecdir@:>@/zfs]]]),
		[zfsexecdir=$withval],
		[zfsexecdir="${libexecdir}/zfs"])

	DEFINE_ZFSEXECDIR='--define "_zfsexecdir $(zfsexecdir)"'

	AC_SUBST([zfsexecdir])
	AC_SUBST(DEFINE_ZFSEXECDIR)
])
