dnl #
dnl # 2.6.28 API change
dnl # Added check_disk_size_change() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CHECK_DISK_SIZE_CHANGE], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[check_disk_size_change],
		[fs/block_dev.c],
		[AC_DEFINE(HAVE_CHECK_DISK_SIZE_CHANGE, 1,
		[check_disk_size_change() is available])],
		[])
])
