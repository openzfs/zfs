dnl #
dnl # Check for ioctl()
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_IOCTL], [
	AC_MSG_CHECKING(for ioctl())
	AC_EGREP_HEADER(ioctl, unistd.h, is_unistd=yes, is_unistd=no)
	AC_EGREP_HEADER(ioctl, sys/ioctl.h, is_sys_ioctl=yes, is_sys_ioctl=no)
	AC_EGREP_HEADER(ioctl, stropts.h, is_stropts=yes, is_stropts=no)

	if test $is_unistd = yes; then
		result=unistd.h
		AC_DEFINE([HAVE_IOCTL_IN_UNISTD_H], 1,
		[Define to 1 if ioctl() defined in <unistd.h>])
	else

		if test $is_sys_ioctl = yes; then
			result=sys/ioctl.h
			AC_DEFINE([HAVE_IOCTL_IN_SYS_IOCTL_H], 1,
			[Define to 1 if ioctl() defined in <sys/ioctl.h>])
		elif test $is_stropts = yes; then
			AC_DEFINE([HAVE_IOCTL_IN_STROPTS_H], 1,
			result=stropts.h
			[Define to 1 if ioctl() defined in <stropts.h>])
		else
			result=no
		fi
	fi

	if test $result = no; then
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([*** Cannot locate ioctl() definition])
	else
		AC_MSG_RESULT(yes)
	fi
])
