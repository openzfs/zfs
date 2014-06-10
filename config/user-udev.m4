AC_DEFUN([ZFS_AC_CONFIG_USER_UDEV], [
	AC_MSG_CHECKING(for udev directories)
	AC_ARG_WITH(udevdir,
		AC_HELP_STRING([--with-udevdir=DIR],
		[install udev helpers @<:@default=check@:>@]),
		udevdir=$withval,udevdir=check)

	AC_ARG_WITH(udevruledir,
		AC_HELP_STRING([--with-udevruledir=DIR],
		[install udev rules @<:@default=check@:>@]),
		udevruledir=$withval,udevruledir=check)

	AS_IF([test "x$udevdir" = xcheck],
	[
		udevdir=$(strings `which udevd` | grep -w lib/udev | grep -Ev '/rules|/devices')
	])

	AS_IF([test "x$udevruledir" = xcheck],
	[
		udevruledir=$(strings `which udevd` | grep -w lib/udev | grep -E '/rules')
	])

	AC_SUBST(udevdir)
	AC_SUBST(udevruledir)
	AC_MSG_RESULT([$udevdir;$udevruledir])
])
