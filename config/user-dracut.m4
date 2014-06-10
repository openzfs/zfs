AC_DEFUN([ZFS_AC_CONFIG_USER_DRACUT], [
	AC_MSG_CHECKING(for dracut directory)
	AC_ARG_WITH(dracutdir,
		AC_HELP_STRING([--with-dracutdir=DIR],
		[install dracut helpers @<:@default=check@:>@]),
		dracutdir=$withval,dracutdir=check)

	AS_IF([test "x$dracutdir" = xcheck],
	[
		dracutdir=$(echo /usr/*/dracut*/modules.d | sed -e 's;/modules.d$;;')
	])

	AC_SUBST(dracutdir)
	AC_MSG_RESULT([$dracutdir])
])
