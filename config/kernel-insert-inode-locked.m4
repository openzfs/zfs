dnl #
dnl # 2.6.28 API change
dnl # Added insert_inode_locked() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INSERT_INODE_LOCKED], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[insert_inode_locked],
		[fs/inode.c],
		[AC_DEFINE(HAVE_INSERT_INODE_LOCKED, 1,
		[insert_inode_locked() is available])],
		[])
])
