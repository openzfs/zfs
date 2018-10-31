dnl #
dnl # ZFS_AC_PYTHON_VERSION(version, [action-if-true], [action-if-false])
dnl #
dnl # Verify Python version
dnl #
AC_DEFUN([ZFS_AC_PYTHON_VERSION], [
	ver_check=`$PYTHON -c "import sys; print (sys.version.split()[[0]] $1)"`
	AS_IF([test "$ver_check" = "True"], [
		m4_ifvaln([$2], [$2])
	], [
		m4_ifvaln([$3], [$3])
	])
])

dnl #
dnl # ZFS_AC_PYTHON_MODULE(module_name, [action-if-true], [action-if-false])
dnl #
dnl # Checks for Python module. Freely inspired by AX_PYTHON_MODULE
dnl # https://www.gnu.org/software/autoconf-archive/ax_python_module.html
dnl # Required by ZFS_AC_CONFIG_ALWAYS_PYZFS.
dnl #
AC_DEFUN([ZFS_AC_PYTHON_MODULE], [
	PYTHON_NAME=`basename $PYTHON`
	AC_MSG_CHECKING([for $PYTHON_NAME module: $1])
	AS_IF([$PYTHON -c "import $1" 2>/dev/null], [
		AC_MSG_RESULT(yes)
		m4_ifvaln([$2], [$2])
	], [
		AC_MSG_RESULT(no)
		m4_ifvaln([$3], [$3])
	])
])

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
		[check],
		[AS_IF([test -x /usr/bin/python3],
			[PYTHON="python3"],
			[AS_IF([test -x /usr/bin/python2],
				[PYTHON="python2"],
				[PYTHON=""]
			)]
		)],
		[2*], [PYTHON="python${with_python}"],
		[*python2*], [PYTHON="${with_python}"],
		[3*], [PYTHON="python${with_python}"],
		[*python3*], [PYTHON="${with_python}"],
		[no], [PYTHON=""],
		[AC_MSG_ERROR([Unknown --with-python value '$with_python'])]
	)

	AS_IF([$PYTHON --version >/dev/null 2>&1], [ /bin/true ], [
		AC_MSG_ERROR([Cannot find $PYTHON in your system path])
	])

	AM_PATH_PYTHON([2.6], [], [:])
	AM_CONDITIONAL([USING_PYTHON], [test "$PYTHON" != :])
	AM_CONDITIONAL([USING_PYTHON_2], [test "${PYTHON_VERSION:0:2}" = "2."])
	AM_CONDITIONAL([USING_PYTHON_3], [test "${PYTHON_VERSION:0:2}" = "3."])

	dnl #
	dnl # Minimum supported Python versions for utilities:
	dnl # Python 2.6.x, or Python 3.4.x
	dnl #
	AS_IF([test "${PYTHON_VERSION:0:2}" = "2."], [
		ZFS_AC_PYTHON_VERSION([>= '2.6'], [ /bin/true ],
			[AC_MSG_ERROR("Python >= 2.6.x is not available")])
	])

	AS_IF([test "${PYTHON_VERSION:0:2}" = "3."], [
		ZFS_AC_PYTHON_VERSION([>= '3.4'], [ /bin/true ],
			[AC_MSG_ERROR("Python >= 3.4.x is not available")])
	])

	dnl #
	dnl # Request that packages be built for a specific Python version.
	dnl #
	AS_IF([test $with_python != check], [
		PYTHON_PKG_VERSION=`echo ${PYTHON} | tr -d 'a-zA-Z.'`
		DEFINE_PYTHON_PKG_VERSION='--define "__use_python_pkg_version '${PYTHON_PKG_VERSION}'"'
		DEFINE_PYTHON_VERSION='--define "__use_python '${PYTHON}'"'
	], [
		DEFINE_PYTHON_VERSION=''
		DEFINE_PYTHON_PKG_VERSION=''
	])

	AC_SUBST(DEFINE_PYTHON_VERSION)
	AC_SUBST(DEFINE_PYTHON_PKG_VERSION)
])
