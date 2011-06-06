dnl #
dnl # 2.6.28 API change
dnl # Added d_obtain_alias() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_OBTAIN_ALIAS], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[d_obtain_alias],
		[fs/dcache.c],
		[AC_DEFINE(HAVE_D_OBTAIN_ALIAS, 1,
		[d_obtain_alias() is available])],
		[])
])
