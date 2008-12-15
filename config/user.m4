dnl #
dnl # Default ZFS user configuration
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER], [
])

AC_DEFINE(ZFS_AC_CONFIG_USER_IOCTL], [
	AC_EGREP_HEADER(ioctl, unistd.h,
		[AC_DEFINE([HAVE_IOCTL_IN_UNISTD_H], 1,
		[Define to 1 if ioctl() is defined in <unistd.h> header])])

	AC_EGREP_HEADER(ioctl, sys/ioctl.h,
		[AC_DEFINE([HAVE_IOCTL_IN_SYS_IOCTL_H], 1,
		[Define to 1 if ioctl() is defined in <sys/ioctl.h> header])])

	AC_EGREP_HEADER(ioctl, stropts.h,
		[AC_DEFINE([HAVE_IOCTL_IN_STROPTS_H], 1,
		[Define to 1 if ioctl() is defined in <stropts.h> header])])
])
