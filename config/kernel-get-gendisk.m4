dnl #
dnl # 2.6.34 API change
dnl # Verify the get_gendisk() symbol is exported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GET_GENDISK], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[get_gendisk],
		[block/genhd.c],
		[AC_DEFINE(HAVE_GET_GENDISK, 1, [get_gendisk() is available])],
		[])
])
