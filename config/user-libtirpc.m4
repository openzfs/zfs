dnl
dnl Check for libtirpc
dnl

AC_DEFUN([ZFS_AC_CONFIG_USER_LIBTIRPC], [
	AC_ARG_WITH(libtirpc,
		AC_HELP_STRING([--with-libtirpc], [compile with libtirpc]),
		[Define to 1 if libtirpc is being used as the RPC library])

	AS_IF([test "x$with_libtirpc" != xno], [
		AC_MSG_CHECKING(for libtirpc)
		AC_CHECK_LIB([tirpc], [xdr_int], [
			AC_DEFINE([WITH_LIBTIRPC], 1, [Define if you have libtirpc])
			PKG_CHECK_MODULES([TIRPC], [libtirpc], [
					AC_SUBST(TIRPC_CFLAGS)
					AC_SUBST(TIRPC_LIBS)
				], [
					AC_SUBST(TIRPC_CFLAGS, ["-I${prefix}/include/tirpc"])
					AC_SUBST(TIRPC_LIBS, ["-ltirpc"])
				])
			AC_MSG_RESULT([$ac_cv_lib_tirpc_xdr_int])
			AS_IF([test "x$with_libtirpc" = xyes],
				[AC_MSG_FAILURE([libtirpc not found])],
				[AC_MSG_RESULT([$ac_cv_lib_tirpc_xdr_int])])
		])
	])
])
