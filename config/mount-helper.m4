AC_DEFUN([ZFS_AC_CONFIG_USER_MOUNT_HELPER], [
	AC_ARG_WITH(mounthelperdir,
		AS_HELP_STRING([--with-mounthelperdir=DIR],
		[install mount.zfs in dir [[$sbindir]]]),
		mounthelperdir=$withval,mounthelperdir=$sbindir)

	AC_SUBST(mounthelperdir)
])
