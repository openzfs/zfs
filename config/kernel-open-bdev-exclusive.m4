dnl #
dnl # 2.6.28 API change
dnl # open/close_bdev_excl() renamed to open/close_bdev_exclusive()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_OPEN_BDEV_EXCLUSIVE], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[open_bdev_exclusive],
		[fs/block_dev.c],
		[AC_DEFINE(HAVE_OPEN_BDEV_EXCLUSIVE, 1,
		[open_bdev_exclusive() is available])],
		[])
])
