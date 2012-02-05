AC_DEFUN([ZFS_AC_CONFIG_USER_DRACUT], [
	AC_ARG_WITH(dracutdir,
		AC_HELP_STRING([--with-dracutdir=DIR],
		[directory containing dracut modules.d [[DATADIR/dracut]]]),
		dracutdir=$withval)

	AC_PATH_PROG(HAVE_DRACUT, dracut)
	AS_IF([test "x$HAVE_DRACUT" != "x" -a "x$dracutdir" = "x"],
	[
		AC_PROG_SED
		AC_PROG_GREP
		AC_MSG_CHECKING([for Dracut modules.d location])
		dracutdir=`$GREP 'dracutbasedir=/' $HAVE_DRACUT | head -n1 | $SED -e 's/.*dracutbasedir=\(.*\)$/\1/'`
		AC_MSG_RESULT([$dracutdir])

	])
	AC_SUBST(dracutdir)
	AM_CONDITIONAL(INSTALL_DRACUT, test "x$dracutdir" != "x")
])
