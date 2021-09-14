dnl #
dnl # Enabled -fsanitize=address if supported by gcc.
dnl #
dnl # LDFLAGS needs -fsanitize=address at all times so libraries compiled with
dnl # it will be linked successfully. CFLAGS will vary by binary being built.
dnl #
dnl # The ASAN_OPTIONS environment variable can be used to further control
dnl # the behavior of binaries and libraries build with -fsanitize=address.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_ASAN], [
	AC_MSG_CHECKING([whether to build with -fsanitize=address support])
	AC_ARG_ENABLE([asan],
		[AS_HELP_STRING([--enable-asan],
		[Enable -fsanitize=address support  @<:@default=no@:>@])],
		[],
		[enable_asan=no])

	AM_CONDITIONAL([ASAN_ENABLED], [test x$enable_asan = xyes])
	AC_SUBST([ASAN_ENABLED], [$enable_asan])
	AC_MSG_RESULT($enable_asan)

	AS_IF([ test "$enable_asan" = "yes" ], [
		AC_MSG_CHECKING([whether $CC supports -fsanitize=address])
		saved_cflags="$CFLAGS"
		CFLAGS="$CFLAGS -Werror -fsanitize=address"
		AC_LINK_IFELSE([
			AC_LANG_SOURCE([[ int main() { return 0; } ]])
		], [
			ASAN_CFLAGS="-fsanitize=address"
			ASAN_LDFLAGS="-fsanitize=address"
			ASAN_ZFS="_with_asan"
			AC_MSG_RESULT([yes])
		], [
			AC_MSG_ERROR([$CC does not support -fsanitize=address])
		])
		CFLAGS="$saved_cflags"
	], [
		ASAN_CFLAGS=""
		ASAN_LDFLAGS=""
		ASAN_ZFS="_without_asan"
	])

	AC_SUBST([ASAN_CFLAGS])
	AC_SUBST([ASAN_LDFLAGS])
	AC_SUBST([ASAN_ZFS])
])

dnl #
dnl # Check if gcc supports -Wframe-larger-than=<size> option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_FRAME_LARGER_THAN], [
	AC_MSG_CHECKING([whether $CC supports -Wframe-larger-than=<size>])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -Wframe-larger-than=4096"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		FRAME_LARGER_THAN="-Wframe-larger-than=4096"
		AC_MSG_RESULT([yes])
	], [
		FRAME_LARGER_THAN=""
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([FRAME_LARGER_THAN])
])

dnl #
dnl # Check if gcc supports -Wno-format-truncation option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_NO_FORMAT_TRUNCATION], [
	AC_MSG_CHECKING([whether $CC supports -Wno-format-truncation])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -Wno-format-truncation"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_FORMAT_TRUNCATION=-Wno-format-truncation
		AC_MSG_RESULT([yes])
	], [
		NO_FORMAT_TRUNCATION=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_FORMAT_TRUNCATION])
])

dnl #
dnl # Check if gcc supports -Wno-format-truncation option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_NO_FORMAT_ZERO_LENGTH], [
	AC_MSG_CHECKING([whether $CC supports -Wno-format-zero-length])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -Wno-format-zero-length"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_FORMAT_ZERO_LENGTH=-Wno-format-zero-length
		AC_MSG_RESULT([yes])
	], [
		NO_FORMAT_ZERO_LENGTH=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_FORMAT_ZERO_LENGTH])
])


dnl #
dnl # Check if gcc supports -Wno-bool-compare option.
dnl #
dnl # We actually invoke gcc with the -Wbool-compare option
dnl # and infer the 'no-' version does or doesn't exist based upon
dnl # the results.  This is required because when checking any of
dnl # no- prefixed options gcc always returns success.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_NO_BOOL_COMPARE], [
	AC_MSG_CHECKING([whether $CC supports -Wno-bool-compare])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -Wbool-compare"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_BOOL_COMPARE=-Wno-bool-compare
		AC_MSG_RESULT([yes])
	], [
		NO_BOOL_COMPARE=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_BOOL_COMPARE])
])

dnl #
dnl # Check if gcc supports -Wno-unused-but-set-variable option.
dnl #
dnl # We actually invoke gcc with the -Wunused-but-set-variable option
dnl # and infer the 'no-' version does or doesn't exist based upon
dnl # the results.  This is required because when checking any of
dnl # no- prefixed options gcc always returns success.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_NO_UNUSED_BUT_SET_VARIABLE], [
	AC_MSG_CHECKING([whether $CC supports -Wno-unused-but-set-variable])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -Wunused-but-set-variable"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_UNUSED_BUT_SET_VARIABLE=-Wno-unused-but-set-variable
		AC_MSG_RESULT([yes])
	], [
		NO_UNUSED_BUT_SET_VARIABLE=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_UNUSED_BUT_SET_VARIABLE])
])

dnl #
dnl # Check if gcc supports -Wimplicit-fallthrough option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_IMPLICIT_FALLTHROUGH], [
	AC_MSG_CHECKING([whether $CC supports -Wimplicit-fallthrough])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -Wimplicit-fallthrough"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		IMPLICIT_FALLTHROUGH=-Wimplicit-fallthrough
		AC_DEFINE([HAVE_IMPLICIT_FALLTHROUGH], 1,
			[Define if compiler supports -Wimplicit-fallthrough])
		AC_MSG_RESULT([yes])
	], [
		IMPLICIT_FALLTHROUGH=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([IMPLICIT_FALLTHROUGH])
])

dnl #
dnl # Check if gcc supports -fno-omit-frame-pointer option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_NO_OMIT_FRAME_POINTER], [
	AC_MSG_CHECKING([whether $CC supports -fno-omit-frame-pointer])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -fno-omit-frame-pointer"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_OMIT_FRAME_POINTER=-fno-omit-frame-pointer
		AC_MSG_RESULT([yes])
	], [
		NO_OMIT_FRAME_POINTER=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_OMIT_FRAME_POINTER])
])

dnl #
dnl # Check if cc supports -fno-ipa-sra option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_CC_NO_IPA_SRA], [
	AC_MSG_CHECKING([whether $CC supports -fno-ipa-sra])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Werror -fno-ipa-sra"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])], [
		NO_IPA_SRA=-fno-ipa-sra
		AC_MSG_RESULT([yes])
	], [
		NO_IPA_SRA=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_IPA_SRA])
])
