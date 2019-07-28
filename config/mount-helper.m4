AC_DEFUN([ZFS_AC_CONFIG_USER_MOUNT_HELPER], [
	AC_ARG_WITH(mounthelperdir,
		AC_HELP_STRING([--with-mounthelperdir=DIR],
		[install mount.zfs in dir [[/sbin]]]),
		mounthelperdir=$withval,mounthelperdir=/sbin)

	DEFINE_MOUNTHELPERDIR='--define "_mounthelperdir $(mounthelperdir)"'

	AC_SUBST(mounthelperdir)
	AC_SUBST(DEFINE_MOUNTHELPERDIR)
])
