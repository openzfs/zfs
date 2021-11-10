# find_system_lib.m4 - Macros to search for a system library.   -*- Autoconf -*-

dnl requires pkg.m4 from pkg-config
dnl requires ax_save_flags.m4 from autoconf-archive
dnl requires ax_restore_flags.m4 from autoconf-archive

dnl ZFS_AC_FIND_SYSTEM_LIBRARY(VARIABLE-PREFIX, MODULE, HEADER, HEADER-PREFIXES, LIBRARY, FUNCTIONS, [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])

AC_DEFUN([ZFS_AC_FIND_SYSTEM_LIBRARY], [
    AC_REQUIRE([PKG_PROG_PKG_CONFIG])

    _header_found=
    _library_found=
    _pc_found=

    AS_IF([test -n "$2"], [PKG_CHECK_MODULES([$1], [$2], [
	_header_found=1
	_library_found=1
	_pc_found=1
    ], [:])])

    # set _header_found/_library_found if the user passed in CFLAGS/LIBS
    AS_IF([test "x$[$1][_CFLAGS]" != x], [_header_found=1])
    AS_IF([test "x$[$1][_LIBS]" != x], [_library_found=1])

    AX_SAVE_FLAGS

    orig_CFLAGS="$CFLAGS"

    for _prefixdir in /usr /usr/local
    do
	AS_VAR_PUSHDEF([header_cache], [ac_cv_header_$3])
	AS_IF([test "x$_prefixdir" != "x/usr"], [
	    [$1][_CFLAGS]="-I$lt_sysroot$_prefixdir/include"
	    AS_IF([test "x$_library_found" = x], [
		[$1][_LIBS]="-L$lt_sysroot$_prefixdir/lib"
	    ])
	])
	CFLAGS="$orig_CFLAGS $[$1][_CFLAGS]"
	AS_UNSET([header_cache])
	AC_CHECK_HEADER([$3], [
	    _header_found=1
	    break
	], [AS_IF([test "x$_header_found" = "x1"], [
	    # if pkg-config or the user set CFLAGS, fail if the header is unusable
	    AC_MSG_FAILURE([header [$3] for library [$5] is not usable])
	])], [AC_INCLUDES_DEFAULT])
	# search for header under HEADER-PREFIXES
	m4_foreach_w([prefix], [$4], [
	    [$1][_CFLAGS]=["-I$lt_sysroot$_prefixdir/include/]prefix["]
	    CFLAGS="$orig_CFLAGS $[$1][_CFLAGS]"
	    AS_UNSET([header_cache])
	    AC_CHECK_HEADER([$3], [
		_header_found=1
		break
	    ], [], [AC_INCLUDES_DEFAULT])
	])
	AS_VAR_POPDEF([header_cache])
    done

    AS_IF([test "x$_header_found" = "x1"], [
	AS_IF([test "x$_library_found" = x], [
	    [$1][_LIBS]="$[$1]_LIBS -l[$5]"
	])
	LDFLAGS="$LDFLAGS $[$1][_LIBS]"

	_libcheck=1
	m4_ifval([$6],
	    [m4_foreach_w([func], [$6], [AC_CHECK_LIB([$5], func, [:], [_libcheck=])])],
	    [AC_CHECK_LIB([$5], [main], [:], [_libcheck=])])

	AS_IF([test "x$_libcheck" = "x1"], [_library_found=1],
	    [test "x$_library_found" = "x1"], [
	    # if pkg-config or the user set LIBS, fail if the library is unusable
	    AC_MSG_FAILURE([library [$5] is not usable])
	])
    ], [test "x$_library_found" = "x1"], [
	# if the user set LIBS, fail if we didn't find the header
	AC_MSG_FAILURE([cannot find header [$3] for library [$5]])
    ])

    AX_RESTORE_FLAGS

    AS_IF([test "x$_header_found" = "x1" && test "x$_library_found" = "x1"], [
	AC_SUBST([$1]_CFLAGS)
	AC_SUBST([$1]_LIBS)
	AS_IF([test "x$_pc_found" = "x1"], [
	    AC_SUBST([$1]_PC, [$2])
	])
	AC_DEFINE([HAVE_][$1], [1], [Define if you have [$5]])
	$7
    ],[dnl ELSE
	AC_SUBST([$1]_CFLAGS, [])
	AC_SUBST([$1]_LIBS, [])
	AC_MSG_WARN([cannot find [$5] via pkg-config or in the standard locations])
	$8
    ])
])
