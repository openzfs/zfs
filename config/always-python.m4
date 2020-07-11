dnl #
dnl # The majority of the python scripts are written to be compatible
dnl # with Python 2.6 and Python 3.4.  Therefore, they may be installed
dnl # and used with either interpreter.  This option is intended to
dnl # to provide a method to specify the default system version, and
dnl # set the PYTHON environment variable accordingly.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_PYTHON], [
	AC_ARG_WITH([python],
		AC_HELP_STRING([--with-python[=VERSION]],
		[default system python version @<:@default=check@:>@]),
		[with_python=$withval],
		[with_python=check])

	AS_CASE([$with_python],
		[check], [AC_CHECK_PROGS([PYTHON], [python3 python2], [:])],
		[2*], [PYTHON="python${with_python}"],
		[*python2*], [PYTHON="${with_python}"],
		[3*], [PYTHON="python${with_python}"],
		[*python3*], [PYTHON="${with_python}"],
		[no], [PYTHON=":"],
		[AC_MSG_ERROR([Unknown --with-python value '$with_python'])]
	)

	dnl #
	dnl # Minimum supported Python versions for utilities:
	dnl # Python 2.6 or Python 3.4
	dnl #
	AM_PATH_PYTHON([], [], [:])
	AS_IF([test -z "$PYTHON_VERSION"], [
		PYTHON_VERSION=$(basename $PYTHON | tr -cd 0-9.)
	])
	PYTHON_MINOR=${PYTHON_VERSION#*\.}

	AS_CASE([$PYTHON_VERSION],
		[2.*], [
			AS_IF([test $PYTHON_MINOR -lt 6],
				[AC_MSG_ERROR("Python >= 2.6 is required")])
		],
		[3.*], [
			AS_IF([test $PYTHON_MINOR -lt 4],
				[AC_MSG_ERROR("Python >= 3.4 is required")])
		],
		[:|2|3], [],
		[PYTHON_VERSION=3]
	)

	AM_CONDITIONAL([USING_PYTHON], [test "$PYTHON" != :])
	AM_CONDITIONAL([USING_PYTHON_2], [test "x${PYTHON_VERSION%%\.*}" = x2])
	AM_CONDITIONAL([USING_PYTHON_3], [test "x${PYTHON_VERSION%%\.*}" = x3])

	AM_COND_IF([USING_PYTHON_2],
		[AC_SUBST([PYTHON_SHEBANG], [python2])],
		[AC_SUBST([PYTHON_SHEBANG], [python3])])

	dnl #
	dnl # Request that packages be built for a specific Python version.
	dnl #
	AS_IF([test "x$with_python" != xcheck], [
		PYTHON_PKG_VERSION=$(echo $PYTHON_VERSION | tr -d .)
		DEFINE_PYTHON_PKG_VERSION='--define "__use_python_pkg_version '${PYTHON_PKG_VERSION}'"'
		DEFINE_PYTHON_VERSION='--define "__use_python '${PYTHON}'"'
	], [
		DEFINE_PYTHON_VERSION=''
		DEFINE_PYTHON_PKG_VERSION=''
	])

	AC_SUBST(DEFINE_PYTHON_VERSION)
	AC_SUBST(DEFINE_PYTHON_PKG_VERSION)
])
