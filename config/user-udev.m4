AC_DEFUN([ZFS_AC_CONFIG_USER_UDEV], [
	PKG_CHECK_MODULES([UDEV], [udev], [have_udev=yes], [have_udev=no])

	AC_MSG_CHECKING(for udev directories XXX $prefix XXX)
	AC_ARG_WITH(udevdir,
		AS_HELP_STRING([--with-udevdir=DIR],
		[install udev helpers @<:@default=check@:>@]),
		[udevdir=$withval],
		[udevdir=check])

	AS_IF([test "x$udevdir" = xcheck], [
		path1=/lib/udev
		path2=$prefix/lib/udev
		default=$path2

		AS_IF([test "x${have_udev}" = xyes],
			[udevdir=`$PKG_CONFIG --variable=udevdir udev`],
			AS_IF([test "$prefix" = "/usr"], [
				AS_IF([test -d "$path1"], [udevdir="$path1"], [
					AS_IF([test -d "$path2"], [udevdir="$path2"],
						[udevdir="$default"])
				])
			], [udevdir="$default"])
		)
	])

	AC_ARG_WITH(udevruledir,
		AS_HELP_STRING([--with-udevruledir=DIR],
		[install udev rules [[UDEVDIR/rules.d]]]),
		[udevruledir=$withval],
		[udevruledir="${udevdir}/rules.d"])

	AC_SUBST(udevdir)
	AC_SUBST(udevruledir)
	AC_MSG_RESULT([$udevdir;$udevruledir])
])
