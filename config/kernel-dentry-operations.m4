dnl #
dnl # 2.6.28 API change
dnl # Added d_obtain_alias() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_OBTAIN_ALIAS], [
	ZFS_LINUX_TEST_SRC([d_obtain_alias], [
		#include <linux/dcache.h>
	], [
		d_obtain_alias(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_OBTAIN_ALIAS], [
	AC_MSG_CHECKING([whether d_obtain_alias() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([d_obtain_alias],
	    [d_obtain_alias], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([d_obtain_alias()])
	])
])

dnl #
dnl # 2.6.38 API change
dnl # Added d_set_d_op() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_SET_D_OP], [
	ZFS_LINUX_TEST_SRC([d_set_d_op], [
		#include <linux/dcache.h>
	], [
		d_set_d_op(NULL, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_SET_D_OP], [
	AC_MSG_CHECKING([whether d_set_d_op() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([d_set_d_op],
	    [d_set_d_op], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([d_set_d_op])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_DENTRY], [
        ZFS_AC_KERNEL_SRC_D_OBTAIN_ALIAS
        ZFS_AC_KERNEL_SRC_D_SET_D_OP
        ZFS_AC_KERNEL_SRC_S_D_OP
])

AC_DEFUN([ZFS_AC_KERNEL_DENTRY], [
        ZFS_AC_KERNEL_D_OBTAIN_ALIAS
        ZFS_AC_KERNEL_D_SET_D_OP
        ZFS_AC_KERNEL_S_D_OP
])
