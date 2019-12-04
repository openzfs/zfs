# find_system_lib.m4 - Macros to search for a system library.   -*- Autoconf -*-

dnl requires pkg.m4 from pkg-config
dnl requires ax_save_flags.m4 from autoconf-archive
dnl requires ax_restore_flags.m4 from autoconf-archive

dnl FIND_SYSTEM_LIBRARY(VARIABLE-PREFIX, MODULE, HEADER, HEADER-PREFIXES, LIBRARY, FUNCTIONS, [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])

AC_DEFUN([FIND_SYSTEM_LIBRARY], [
    AC_REQUIRE([PKG_PROG_PKG_CONFIG])

    _library_found=

    PKG_CHECK_MODULES([$1], [$2], [_library_found=1], [
        AS_IF([test -f /usr/include/[$3]], [
            AC_SUBST([$1][_CFLAGS], [])
            AC_SUBST([$1][_LIBS], ["-l[$5]]")
            _library_found=1
        ],[ AS_IF([test -f /usr/local/include/[$3]], [
            AC_SUBST([$1][_CFLAGS], ["-I/usr/local/include"])
            AC_SUBST([$1][_LIBS], ["-L/usr/local -l[$5]]")
            _library_found=1
        ],[dnl ELSE
            m4_foreach([prefix], [$4], [
                AS_IF([test "x$_library_found" != "x1"], [
                    AS_IF([test -f [/usr/include/]prefix[/][$3]], [
                        AC_SUBST([$1][_CFLAGS], ["[-I/usr/include/]prefix["]])
                        AC_SUBST([$1][_LIBS], ["-l[$5]]")
                        _library_found=1
                    ],[ AS_IF([test -f [/usr/local/include/]prefix[/][$3]], [
                        AC_SUBST([$1][_CFLAGS], ["[-I/usr/local/include/]prefix["]])
                        AC_SUBST([$1][_LIBS], ["-L/usr/local -l[$5]"])
                        _library_found=1
                    ])])
                ])
            ])
        ])])

        AS_IF([test -z "$_library_found"], [
            AC_MSG_WARN([cannot find [$2] via pkg-config or in the standard locations])
        ])
    ])

    dnl do some further sanity checks

    AS_IF([test -n "$_library_found"], [
        AX_SAVE_FLAGS

        CPPFLAGS="$CPPFLAGS $(echo $[$1][_CFLAGS] | sed 's/-include */-include-/g; s/^/ /; s/ [^-][^ ]*//g; s/ -[^Ii][^ ]*//g; s/-include-/-include /g; s/^ //;')"
        CFLAGS="$CFLAGS $[$1][_CFLAGS]"
        LDFLAGS="$LDFLAGS $[$1][_LIBS]"

        AC_CHECK_HEADER([$3], [], [
            AC_MSG_WARN([header [$3] for library [$2] is not usable])
            _library_found=
        ])

        m4_foreach([func], [$6], [
            AC_CHECK_LIB([$5], func, [], [
                AC_MSG_WARN([cannot find ]func[ in library [$5]])
                _library_found=
            ])
        ])
        
        AX_RESTORE_FLAGS
    ])

    AS_IF([test -n "$_library_found"], [
        :;$7
    ],[dnl ELSE
        :;$8
    ])
])
