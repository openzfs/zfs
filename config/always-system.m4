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
		*darwin*)
			AC_DEFINE([SYSTEM_MACOS], [1],
				[True if ZFS is to be compiled for a macOS system])
			ac_system="macOS"
			ac_system_l="macos"
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
	AM_CONDITIONAL([BUILD_MACOS], [test "x$ac_system" = "xmacOS"])
])
