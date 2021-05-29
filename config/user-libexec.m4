AC_DEFUN([ZFS_AC_CONFIG_USER_ZFSEXEC], [
	AC_REQUIRE([AC_LIB_PREPARE_PREFIX])

	AC_ARG_WITH(zfsexecdir,
		AS_HELP_STRING([--with-zfsexecdir=DIR],
		[install scripts [[@<:@libexecdir@:>@/zfs]]]),
		[zfsexecdir=$withval],
		[zfsexecdir="${libexecdir}/zfs"])

	AC_SUBST([zfsexecdir])
	AC_LIB_WITH_FINAL_PREFIX([
		eval true_zfsexecdir=\"$zfsexecdir\"
		AC_DEFINE_UNQUOTED([ZFSEXECDIR], ["$true_zfsexecdir"], [location of non-user-runnable executables])
	])
])
