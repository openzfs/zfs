dnl #
dnl # Check for a libfetch - either fetch(3) or libcurl.
dnl #
dnl # There are two configuration dimensions:
dnl #   * fetch(3) vs libcurl
dnl #   * static vs dynamic
dnl #
dnl # fetch(3) is only dynamic.
dnl # We use sover 6, which first appeared in FreeBSD 8.0-RELEASE.
dnl #
dnl # libcurl development packages include curl-config(1) – we want:
dnl #   * HTTPS support
dnl #   * version at least 7.16 (October 2006), for sover 4
dnl #   * to decide if it's static or not
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBFETCH], [
	AC_MSG_CHECKING([for libfetch])
	LIBFETCH_LIBS=
	LIBFETCH_IS_FETCH=0
	LIBFETCH_IS_LIBCURL=0
	LIBFETCH_DYNAMIC=0
	LIBFETCH_SONAME=
	have_libfetch=

	saved_libs="$LIBS"
	LIBS="$LIBS -lfetch"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[
		#include <sys/param.h>
		#include <stdio.h>
		#include <fetch.h>
	]], [fetchGetURL("", "");])], [
		have_libfetch=1
		LIBFETCH_IS_FETCH=1
		LIBFETCH_DYNAMIC=1
		LIBFETCH_SONAME="libfetch.so.6"
		LIBFETCH_LIBS="-ldl"
		AC_MSG_RESULT([fetch(3)])
	], [])
	LIBS="$saved_libs"

	if test -z "$have_libfetch"; then
		if curl-config --protocols 2>/dev/null | grep -q HTTPS &&
		    test "$(printf "%u" "0x$(curl-config --vernum)")" -ge "$(printf "%u" "0x071000")"; then
			have_libfetch=1
			LIBFETCH_IS_LIBCURL=1
			if test "$(curl-config --built-shared)" = "yes"; then
				LIBFETCH_DYNAMIC=1
				LIBFETCH_SONAME="libcurl.so.4"
				LIBFETCH_LIBS="-ldl"
				AC_MSG_RESULT([libcurl])
			else
				LIBFETCH_LIBS="$(curl-config --libs)"
				AC_MSG_RESULT([libcurl (static)])
			fi

			CCFLAGS="$CCFLAGS $(curl-config --cflags)"
		fi
	fi

	if test -z "$have_libfetch"; then
		AC_MSG_RESULT([none])
	fi

	AC_SUBST([LIBFETCH_LIBS])
	AC_SUBST([LIBFETCH_DYNAMIC])
	AC_SUBST([LIBFETCH_SONAME])
	AC_DEFINE_UNQUOTED([LIBFETCH_IS_FETCH], [$LIBFETCH_IS_FETCH], [libfetch is fetch(3)])
	AC_DEFINE_UNQUOTED([LIBFETCH_IS_LIBCURL], [$LIBFETCH_IS_LIBCURL], [libfetch is libcurl])
	AC_DEFINE_UNQUOTED([LIBFETCH_DYNAMIC], [$LIBFETCH_DYNAMIC], [whether the chosen libfetch is to be loaded at run-time])
	AC_DEFINE_UNQUOTED([LIBFETCH_SONAME], ["$LIBFETCH_SONAME"], [soname of chosen libfetch])
])
