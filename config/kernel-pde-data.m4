dnl #
dnl # 5.17 API: PDE_DATA() renamed to pde_data(),
dnl # 359745d78351c6f5442435f81549f0207ece28aa ("proc: remove PDE_DATA() completely")
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PDE_DATA], [
	ZFS_LINUX_TEST_SRC([pde_data], [
		#include <linux/proc_fs.h>
	], [
		pde_data(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PDE_DATA], [
	AC_MSG_CHECKING([whether pde_data() is lowercase])
	ZFS_LINUX_TEST_RESULT([pde_data], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(SPL_PDE_DATA, pde_data, [pde_data() is pde_data()])
	], [
		AC_MSG_RESULT(no)
		AC_DEFINE(SPL_PDE_DATA, PDE_DATA, [pde_data() is PDE_DATA()])
	])
])
