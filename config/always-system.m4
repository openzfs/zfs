dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Set the target system
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_SYSTEM], [
	AC_MSG_CHECKING([for system type ($host_os)])
	case $host_os in
		*linux*)
			AC_DEFINE([SYSTEM_LINUX], [1],
				[True if ZFS is to be compiled for a Linux system])
			ac_system="Linux"
			ac_system_l="linux"
			;;
		*freebsd*)
			AC_DEFINE([SYSTEM_FREEBSD], [1],
				[True if ZFS is to be compiled for a FreeBSD system])
			ac_system="FreeBSD"
			ac_system_l="freebsd"
			;;
		*netbsd*)
			AC_DEFINE([SYSTEM_NETBSD], [1],
				[True if ZFS is to be compiled for a NetBSD system])
			ac_system="NetBSD"
			ac_system_l="netbsd"
			;;
		*)
			ac_system="unknown"
			ac_system_l="unknown"
			;;
	esac
	AC_MSG_RESULT([$ac_system])
	AC_SUBST([ac_system])
	AC_SUBST([ac_system_l])

	AM_CONDITIONAL([BUILD_LINUX], [test "x$ac_system" = "xLinux"])
	AM_CONDITIONAL([BUILD_FREEBSD], [test "x$ac_system" = "xFreeBSD"])
	AM_CONDITIONAL([BUILD_NETBSD], [test "x$ac_system" = "xNetBSD"])

	dnl # Cross builds (NetBSD build.sh): pass the target DESTDIR so
	dnl # we never search the host /usr/include.  Native builds leave
	dnl # this empty; the compiler's default sysroot is enough.
	AC_ARG_WITH([netbsd-sysroot],
		[AS_HELP_STRING([--with-netbsd-sysroot=DIR],
			[compiler --sysroot for NetBSD cross builds])],
		[netbsd_sysroot="$withval"],
		[netbsd_sysroot="${NETBSD_DESTDIR:-}"])
	AC_SUBST([NETBSD_SYSROOT], [$netbsd_sysroot])
	AM_CONDITIONAL([NETBSD_SYSROOT],
		[test "x$ac_system" = "xNetBSD" && test -n "$netbsd_sysroot"])
	AS_IF([test -n "$netbsd_sysroot"],
		[AC_MSG_NOTICE([NetBSD sysroot: $netbsd_sysroot])])
])
