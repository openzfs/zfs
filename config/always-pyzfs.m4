dnl #
dnl # ZFS_AC_PYTHON_MODULE(module_name, [action-if-true], [action-if-false])
dnl #
dnl # Checks for Python module. Freely inspired by AX_PYTHON_MODULE
dnl # https://www.gnu.org/software/autoconf-archive/ax_python_module.html
dnl # Required by ZFS_AC_CONFIG_ALWAYS_PYZFS.
dnl #
AC_DEFUN([ZFS_AC_PYTHON_MODULE], [
	PYTHON_NAME=$(basename $PYTHON)
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
dnl # Determines if pyzfs can be built, requires Python 2.7 or later.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_PYZFS], [
	AC_ARG_ENABLE([pyzfs],
		AC_HELP_STRING([--enable-pyzfs],
		[install libzfs_core python bindings @<:@default=check@:>@]),
		[enable_pyzfs=$enableval],
		[enable_pyzfs=check])

	dnl #
	dnl # Packages for pyzfs specifically enabled/disabled.
	dnl #
	AS_IF([test "x$enable_pyzfs" != xcheck], [
		AS_IF([test "x$enable_pyzfs" = xyes], [
			DEFINE_PYZFS='--with pyzfs'
		], [
			DEFINE_PYZFS='--without pyzfs'
		])
	], [
		AS_IF([test "$PYTHON" != :], [
			DEFINE_PYZFS=''
		], [
			enable_pyzfs=no
			DEFINE_PYZFS='--without pyzfs'
		])
	])
	AC_SUBST(DEFINE_PYZFS)

	dnl #
	dnl # Require python-devel libraries
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck  -o "x$enable_pyzfs" = xyes], [
		AS_CASE([$PYTHON_VERSION],
			[3.*], [PYTHON_REQUIRED_VERSION=">= '3.4.0'"],
			[2.*], [PYTHON_REQUIRED_VERSION=">= '2.7.0'"],
			[AC_MSG_ERROR("Python $PYTHON_VERSION unknown")]
		)

		AX_PYTHON_DEVEL([$PYTHON_REQUIRED_VERSION], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("Python $PYTHON_REQUIRED_VERSION development library is not installed")
			], [test "x$enable_pyzfs" != xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Python "setuptools" module is required to build and install pyzfs
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck -o "x$enable_pyzfs" = xyes], [
		ZFS_AC_PYTHON_MODULE([setuptools], [], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("Python $PYTHON_VERSION setuptools is not installed")
			], [test "x$enable_pyzfs" != xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Python "cffi" module is required to run pyzfs
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck -o "x$enable_pyzfs" = xyes], [
		ZFS_AC_PYTHON_MODULE([cffi], [], [
			AS_IF([test "x$enable_pyzfs" = xyes], [
				AC_MSG_ERROR("Python $PYTHON_VERSION cffi is not installed")
			], [test "x$enable_pyzfs" != xno], [
				enable_pyzfs=no
			])
		])
	])

	dnl #
	dnl # Set enable_pyzfs to 'yes' if every check passed
	dnl #
	AS_IF([test "x$enable_pyzfs" = xcheck], [enable_pyzfs=yes])

	AM_CONDITIONAL([PYZFS_ENABLED], [test "x$enable_pyzfs" = xyes])
	AC_SUBST([PYZFS_ENABLED], [$enable_pyzfs])
	AC_SUBST(pythonsitedir, [$PYTHON_SITE_PKG])

	AC_MSG_CHECKING([whether to enable pyzfs: ])
	AC_MSG_RESULT($enable_pyzfs)
])
