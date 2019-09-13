dnl #
dnl # 3.10 API change,
dnl # PDE is replaced by PDE_DATA
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PDE_DATA], [
	ZFS_LINUX_TEST_SRC([pde_data], [
		#include <linux/proc_fs.h>
	], [
		PDE_DATA(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PDE_DATA], [
	AC_MSG_CHECKING([whether PDE_DATA() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([pde_data], [PDE_DATA], [], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PDE_DATA, 1, [PDE_DATA is available])
	],[
		AC_MSG_RESULT(no)
	])
])
