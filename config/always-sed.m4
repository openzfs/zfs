dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Set the flags used for sed in-place edits.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_SED], [
	AC_REQUIRE([AC_PROG_SED])dnl
	AC_CACHE_CHECK([for sed --in-place], [ac_cv_inplace], [
		tmpfile=`mktemp conftest.XXXXXX 2>/dev/null`
		AS_IF([test -z "$tmpfile"], [tmpfile="conftest.sedinpl.$$"])
		echo foo >$tmpfile
		AS_IF([$SED --in-place 's#foo#bar#' $tmpfile >/dev/null 2>&1],
		      [ac_cv_inplace="--in-place"], [
			echo foo >$tmpfile
			AS_IF([$SED -i '' 's#foo#bar#' $tmpfile >/dev/null 2>&1],
			      [ac_cv_inplace="-i ''"], [
				echo foo >$tmpfile
				AS_IF([$SED -i 's#foo#bar#' $tmpfile >/dev/null 2>&1],
				      [ac_cv_inplace="-i"],
				      [rm -f $tmpfile
				       AC_MSG_ERROR([$SED does not support in-place])])
			])
		])
		rm -f $tmpfile
	])
	AC_SUBST([ac_inplace], [$ac_cv_inplace])
])
