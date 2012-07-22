dnl #
dnl # 3.5.0 API change
dnl # torvalds/linux@90324cc1b11a211e37eabd8cb863e1a1561d6b1d renamed
dnl # end_writeback() to clear_inode().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CLEAR_INODE], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[clear_inode],
		[fs/inode.c],
		[AC_DEFINE(HAVE_CLEAR_INODE, 1,
		[clear_inode() is available])],
		[])
])
