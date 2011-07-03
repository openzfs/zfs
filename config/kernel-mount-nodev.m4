dnl #
dnl # 2.6.39 API change
dnl # The .get_sb callback has been replaced by a .mount callback
dnl # in the file_system_type structure.  When using the new
dnl # interface the caller must now use the mount_nodev() helper.
dnl # This updated callback and helper no longer pass the vfsmount.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MOUNT_NODEV], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[mount_nodev],
		[fs/super.c],
		[AC_DEFINE(HAVE_MOUNT_NODEV, 1,
		[mount_nodev() is available])],
		[])
])
