dnl #
dnl # Detect QAT related configurations.
dnl #
dnl # If configuring --with-qat=PATH, HAVE_QAT would be
dnl # defined for C code, ICP_ROOT would be defined for Makefile,
dnl # CONFIG_QAT_TRUE would be defined for Makefile.in
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_QAT], [
	AS_IF([test "$ZFS_CONFIG" = kernel -o "$ZFS_CONFIG" = all],
	[
		AC_MSG_CHECKING([whether qat acceleration is configured])
		dnl #
		dnl # Assign path to qat source folder.
		dnl #
		AC_ARG_WITH([qat],
			AS_HELP_STRING([--with-qat=PATH],
			[Configure qat with its source directory path]),
			[qatsrc="$withval"])

		AS_IF([test "x$qatsrc" = x],
		[
			AC_MSG_RESULT([qat is not configured])
		],
		[
			AC_MSG_RESULT([qat is configured with "$qatsrc"])

			AC_MSG_CHECKING([qat source directory])
			AS_IF([ test ! -d "$qatsrc/quickassist"], [
				AC_MSG_ERROR([
	*** Directory $qatsrc/quickassist doesn't exist.
	*** Please specify a location of qat source with option
	*** '--with-qat=PATH'.])
			])
			AC_MSG_RESULT([$qatsrc/quickassist exists])
			ICP_ROOT=${qatsrc}
			AC_SUBST(ICP_ROOT)
			AC_DEFINE(HAVE_QAT, 1,
				[qat is enabled and existed])
		])
	],
	[])

	AM_CONDITIONAL([CONFIG_QAT],
		[test "$ZFS_CONFIG" = kernel -o "$ZFS_CONFIG" = all] &&
		[test "x$qatsrc" != x ])
])

