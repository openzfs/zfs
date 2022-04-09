AC_DEFUN([ZFS_AC_CONFIG_USER_SYSVINIT], [
	AC_ARG_ENABLE(sysvinit,
		AS_HELP_STRING([--enable-sysvinit],
		[install SysV init scripts [default: yes]]),
		[], enable_sysvinit=yes)

	ZFS_INIT_SYSV=$enable_sysvinit
])
