dnl #
dnl # Set the flags used for sed in-place edits.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_SED], [
	ac_inplace=""
	AC_CACHE_CHECK([for sed --in-place], [ac_cv_path_SED],
		[AC_PATH_PROGS_FEATURE_CHECK([SED], [sed],
			[[tmpfile=$(mktemp)
			  echo foo > $tmpfile
			  $ac_path_SED --in-place 's#foo#bar#' $tmpfile \
			  && ac_cv_path_SED=$ac_path_SED
			  rm $tmpfile]],
			[ac_inplace="-i ''"])])
	AS_IF([test "x$ac_inplace" = "x"], [ac_inplace="--in-place"])
	AC_SUBST([ac_inplace])
])
