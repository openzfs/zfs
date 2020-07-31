AC_DEFUN([ZFS_AC_CONFIG_USER_PAM], [
	AC_ARG_ENABLE([pam],
		AS_HELP_STRING([--enable-pam],
		[install pam_zfs_key module [[default: check]]]),
		[enable_pam=$enableval],
		[enable_pam=check])

	AC_ARG_WITH(pammoduledir,
		AS_HELP_STRING([--with-pammoduledir=DIR],
		[install pam module in dir [[$libdir/security]]]),
		[pammoduledir="$withval"],[pammoduledir=$libdir/security])

	AC_ARG_WITH(pamconfigsdir,
		AS_HELP_STRING([--with-pamconfigsdir=DIR],
		[install pam-config files in dir [DATADIR/pam-configs]]),
		[pamconfigsdir="$withval"],
		[pamconfigsdir='${datadir}/pam-configs'])

	AS_IF([test "x$enable_pam" != "xno"], [
		AC_CHECK_HEADERS([security/pam_modules.h], [
			enable_pam=yes
		], [
			AS_IF([test "x$enable_pam" = "xyes"], [
				AC_MSG_FAILURE([
	*** security/pam_modules.h missing, libpam0g-dev package required
				])
			],[
				enable_pam=no
			])
		])
	])
	AS_IF([test "x$enable_pam" = "xyes"], [
		DEFINE_PAM='--with pam'
	])
	AC_SUBST(DEFINE_PAM)
	AC_SUBST(pammoduledir)
	AC_SUBST(pamconfigsdir)
])
