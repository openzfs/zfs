AC_DEFUN([AC_MSG_COMMAND_KSH], [
	KSH=ksh

	AC_MSG_CHECKING([whether $KSH is available])

	AS_IF([tmp=$($KSH --version 2>&1)], [
		KSH=$(echo $tmp | $AWK '{ print $[5]" "$[6] }')
		HAVE_KSH=yes
		AC_MSG_RESULT([$HAVE_KSH ($KSH_VERSION)])
	],[
		HAVE_KSH=no
		AC_MSG_RESULT([$HAVE_KSH])
	])

	AC_SUBST(HAVE_KSH)
	AC_SUBST(KSH)
	AC_SUBST(KSH_VERSION)
])
