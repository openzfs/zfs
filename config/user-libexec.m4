AC_DEFUN([ZFS_AC_CONFIG_USER_ZFSEXEC], [
	AC_ARG_WITH(zfsexecdir,
		AS_HELP_STRING([--with-zfsexecdir=DIR],
		[install scripts [[@<:@libexecdir@:>@/zfs]]]),
		[zfsexecdir=$withval],
		[zfsexecdir="${libexecdir}/zfs"])

	AC_SUBST([zfsexecdir])
])
