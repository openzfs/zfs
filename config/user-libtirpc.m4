dnl #
dnl # Check for libtirpc - may be needed for xdr functionality
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBTIRPC], [
	AC_ARG_WITH([tirpc],
	    [AS_HELP_STRING([--with-tirpc],
		[use tirpc for xdr encoding @<:@default=check@:>@])],
	    [],
	    [with_tirpc=check])

	have_xdr=

        AS_IF([test "x$with_tirpc" != "xyes"], [
	    AC_SEARCH_LIBS([xdrmem_create], [], [have_xdr=1], [
		AS_IF([test "x$with_tirpc" = "xno"], [
		    AC_MSG_FAILURE([xdrmem_create() requires sunrpc support in libc if not using libtirpc])
		])
	    ])
        ])

	AS_IF([test "x$have_xdr" = "x"], [
            ZFS_AC_FIND_SYSTEM_LIBRARY(LIBTIRPC, [libtirpc], [rpc/xdr.h], [tirpc], [tirpc], [xdrmem_create], [], [
		AS_IF([test "x$with_tirpc" = "xyes"], [
		    AC_MSG_FAILURE([--with-tirpc was given, but libtirpc is not available, try installing libtirpc-devel])
		],[dnl ELSE
		    AC_MSG_FAILURE([neither libc sunrpc support nor libtirpc is available, try installing libtirpc-devel])
		])
	    ])
	])
])
