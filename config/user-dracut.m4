AC_DEFUN([ZFS_AC_CONFIG_USER_DRACUT], [
	AC_MSG_CHECKING(for dracut directory XXX prefix=$prefix adp=$ac_default_prefix XXX)
	AC_ARG_WITH([dracutdir],
		AS_HELP_STRING([--with-dracutdir=DIR],
		[install dracut helpers @<:@default=check@:>@]),
		[dracutdir=$withval],
		[dracutdir=check])

	AS_IF([test "x$dracutdir" = xcheck], [
		AS_IF([test "$prefix" != "NONE"],
			[tprefix="$prefix"],
			[tprefix="$ac_default_prefix"]
		)

		path1='$prefix/share/dracut'
		path2='$prefix/lib/dracut'
		default="$path2"

		AS_IF([test -d "$tprefix/share/dracut"], [dracutdir="$path1"], [
			AS_IF([test -d "$tprefix/lib/dracut"], [dracutdir="$path2"],
				[dracutdir="$default"])
		])
	])

	AC_SUBST(dracutdir)
	AC_MSG_RESULT([$dracutdir])
])
