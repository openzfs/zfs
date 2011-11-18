dnl #
dnl # 2.6.34 API change
dnl # The bdi_setup_and_register() helper function is avilable and
dnl # exported by the kernel.  This is a trivial helper function but
dnl # using it significantly simplifies the code surrounding setting
dnl # up and tearing down the bdi structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDI_SETUP_AND_REGISTER], [
	ZFS_CHECK_SYMBOL_EXPORT(
		[bdi_setup_and_register],
		[mm/backing-dev.c],
		[AC_DEFINE(HAVE_BDI_SETUP_AND_REGISTER, 1,
		[bdi_setup_and_register() is available])],
		[])
])
