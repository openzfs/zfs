dnl #
dnl # ZFS_AC_PYTHON_MODULE(module_name, [action-if-true], [action-if-false])
dnl #
dnl # Checks for Python module. Freely inspired by AX_PYTHON_MODULE
dnl # https://www.gnu.org/software/autoconf-archive/ax_python_module.html
dnl #
AC_DEFUN([ZFS_AC_PYTHON_MODULE],[
	PYTHON_NAME=`basename $PYTHON`
	AC_MSG_CHECKING([for $PYTHON_NAME module: $1])
	$PYTHON -c "import $1" 2>/dev/null
	if test $? -eq 0;
	then
		AC_MSG_RESULT(yes)
		m4_ifvaln([$2], [$2])
	else
		AC_MSG_RESULT(no)
		m4_ifvaln([$3], [$3])
	fi
])

dnl #
dnl # ZFS_AC_PYTHON_VERSION(version, [action-if-true], [action-if-false])
dnl #
dnl # Verify Python version
dnl #
AC_DEFUN([ZFS_AC_PYTHON_VERSION], [
	AC_MSG_CHECKING([for a version of Python $1])
	version_check=`$PYTHON -c "import sys; print (sys.version.split()[[0]] $1)"`
	if test "$version_check" = "True";
	then
		AC_MSG_RESULT(yes)
		m4_ifvaln([$2], [$2])
	else
		AC_MSG_RESULT(no)
		m4_ifvaln([$3], [$3])
	fi

])

AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_PYZFS], [
	PYTHON_REQUIRED_VERSION="<= '2.7.x'"

	AC_ARG_ENABLE([pyzfs],
		AC_HELP_STRING([--enable-pyzfs],
		[install libzfs_core python bindings @<:@default=check@:>@]),
		[enable_pyzfs=$enableval],
		[enable_pyzfs=check])

	AM_PATH_PYTHON([2.7], [], [
		AS_IF([test "x$enable_pyzfs" = xyes], [
			AC_MSG_ERROR("python >= 2.7 is not installed")
		], [test ! "x$enable_pyzfs" = xno], [
			enable_pyzfs=no
		])
	])
	AM_CONDITIONAL([HAVE_PYTHON], [test "$PYTHON" != :])

	dnl #
	dnl # Python 2.7.x is supported, other versions (3.5) are not yet
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck], [
		ZFS_AC_PYTHON_VERSION([$PYTHON_REQUIRED_VERSION], [], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("Python $PYTHON_REQUIRED_VERSION is not available")
			], [test ! "x$enable_pyzfs" = xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Require python-devel libraries
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck], [
		AX_PYTHON_DEVEL([$PYTHON_REQUIRED_VERSION], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("Python development library is not available")
			], [test ! "x$enable_pyzfs" = xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Python "setuptools" module is required to build and install pyzfs
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck], [
		ZFS_AC_PYTHON_MODULE([setuptools], [], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("python-setuptools is not installed")
			], [test ! "x$enable_pyzfs" = xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Python "cffi" module is required to run pyzfs
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck], [
		ZFS_AC_PYTHON_MODULE([cffi], [], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("python-cffi is not installed")
			], [test ! "x$enable_pyzfs" = xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Set enable_pyzfs to 'yes' if every check passed
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck], [enable_pyzfs=yes])

	AM_CONDITIONAL([PYZFS_ENABLED], [test x$enable_pyzfs = xyes])
	AC_SUBST([PYZFS_ENABLED], [$enable_pyzfs])

	AS_IF([test "x$enable_pyzfs" = xyes], [
		DEFINE_PYZFS='--define "_pyzfs 1"'
	],[
		DEFINE_PYZFS=''
	])
	AC_SUBST(DEFINE_PYZFS)
	AC_SUBST(pythonsitedir, [$PYTHON_SITE_PKG])
])
