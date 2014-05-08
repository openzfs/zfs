dnl #
dnl # 2.6.34 API change
dnl # The bdi_setup_and_register() helper function is avaliable and
dnl # exported by the kernel.  This is a trivial helper function but
dnl # using it significantly simplifies the code surrounding setting
dnl # up and tearing down the bdi structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDI_SETUP_AND_REGISTER],
	[AC_MSG_CHECKING([whether bdi_setup_and_register() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/backing-dev.h>
	], [
		int r = bdi_setup_and_register(NULL, NULL, 0);
		r = *(&r);
	], [bdi_setup_and_register], [mm/backing-dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDI_SETUP_AND_REGISTER, 1,
		          [bdi_setup_and_register() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
