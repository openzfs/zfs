dnl
dnl musl support
dnl

AC_DEFUN([ZFS_AC_CONFIG_USER_MUSL], [
AH_TEMPLATE([HAVE_MUSL],
  [Define to 1 if musl is being used as the C library])
AH_TEMPLATE([HAVE_TIRPC],
  [Define to 1 if libtirpc is being used as the RPC library])
AC_ARG_ENABLE(musl,
AC_HELP_STRING([--enable-musl], [compile with musl as the C library]),
[if test x$enableval = xyes; then
  AC_DEFINE([HAVE_MUSL], 1, [Define if you have musl])
  PKG_CHECK_MODULES([TIRPC],[libtirpc])
  AC_DEFINE([HAVE_TIRPC], 1, [Define if you have libtirpc])
  AC_SUBST(TIRPC_CFLAGS)
  AC_SUBST(TIRPC_LIBS)
fi])
])
