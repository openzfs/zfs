dnl #
dnl # 3.4.0 API change
dnl # Added d_make_root() to replace previous d_alloc_root() function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_MAKE_ROOT], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[d_make_root],
		[fs/dcache.c],
		[AC_DEFINE(HAVE_D_MAKE_ROOT, 1,
		[d_make_root() is available])],
		[])
])
