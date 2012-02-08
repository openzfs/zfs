dnl # Decide where and whether to install Dracut module.
dnl # Default is detect if dracut script is present and
dnl # parse it to detect where its dracutbasedir points.
dnl # If no script is found, the module isn't installed.
dnl # Explicit with-dracut=... or without-dracut overrides.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_DRACUT], [
	AC_ARG_WITH([dracutdir],
		[AS_HELP_STRING([--with-dracutdir=DIR],
		[directory containing dracut modules.d @<:@default=check@:>@])],
		[with_dracutdir=$withval],
		[with_dracutdir=check])

	AS_IF([test "x$with_dracutdir" = xcheck], 
	[
		AC_PATH_PROG([HAVE_DRACUT], [dracut])
		AS_IF([test "x$HAVE_DRACUT" != "x"],
		[
			AC_MSG_CHECKING([for Dracut modules.d location])
			dracutdir=`$GREP 'dracutbasedir=/' $HAVE_DRACUT | head -n1 | $SED -e 's/.*dracutbasedir=\(.*\)$/\1/'`
			AC_MSG_RESULT([$dracutdir])
		])
	],
	[test "x$with_dracutdir" != xno], [
		dracutdir="$with_dracutdir"
	])
	
	AC_SUBST(dracutdir)
])
