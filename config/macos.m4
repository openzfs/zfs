

AC_DEFUN([ZFS_AC_MACOS_IMPURE_ENABLE], [
	AC_ARG_ENABLE(macos_impure,
		AS_HELP_STRING([--enable-macos-impure],
		[Use XNU Private.exports [[default: no]]]),
		[CPPFLAGS="$CPPFLAGS -DMACOS_IMPURE"],
		[])
])


