AC_DEFUN([ZFS_AC_CONFIG_USER_UDEV], [
	PKG_CHECK_MODULES([UDEV], [udev], [have_udev=yes], [have_udev=no])

	AC_MSG_CHECKING(for udev directories XXX $prefix XXX)
	AC_ARG_WITH(udevdir,
		AS_HELP_STRING([--with-udevdir=DIR],
		[install udev helpers @<:@default=check@:>@]),
		[udevdir=$withval],
		[udevdir=check])

	AS_IF([test "x$udevdir" = xcheck], [
		AS_IF([test "x$prefix" != "xNONE"],
			[tprefix="$prefix"],
			[tprefix="$ac_default_prefix"]
		)

		AS_IF([test "x$tprefix" = "x/usr"], [
			AS_IF([test "x${have_udev}" = xyes],
				[udevdir=`$PKG_CONFIG --variable=udevdir udev`],
				AS_IF([test -d '/lib/udev' -a ! -L '/lib'],
					[udevdir="/lib/udev"],
					[udevdir="/usr/lib/udev"]))
		], [udevdir='${prefix}/lib/udev'])
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
