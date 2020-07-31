dnl #
dnl # Set the flags used for sed in-place edits.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_SED], [
	AC_REQUIRE([AC_PROG_SED])dnl
	AC_CACHE_CHECK([for sed --in-place], [ac_cv_inplace], [
		tmpfile=$(mktemp conftest.XXX)
		echo foo >$tmpfile
		AS_IF([$SED --in-place 's#foo#bar#' $tmpfile 2>/dev/null],
		      [ac_cv_inplace="--in-place"],
		      [$SED -i '' 's#foo#bar#' $tmpfile 2>/dev/null],
		      [ac_cv_inplace="-i ''"],
		      [AC_MSG_ERROR([$SED does not support in-place])])
	])
	AC_SUBST([ac_inplace], [$ac_cv_inplace])
])
