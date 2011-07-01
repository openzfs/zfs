dnl #
dnl # 2.6.35 API change
dnl # Added truncate_setsize() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TRUNCATE_SETSIZE], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[truncate_setsize],
		[mm/truncate.c],
		[AC_DEFINE(HAVE_TRUNCATE_SETSIZE, 1,
		[truncate_setsize() is available])],
		[])
])
