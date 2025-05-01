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
dnl # 6.17 API change
dnl # d_set_d_op() removed. No direct replacement.
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
	ZFS_LINUX_TEST_RESULT([d_set_d_op], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_SET_D_OP, 1,
		    [Define if d_set_d_op() is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 6.17 API change
dnl # sb->s_d_op removed; set_default_d_op(sb, dop) added
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SET_DEFAULT_D_OP], [
	ZFS_LINUX_TEST_SRC([set_default_d_op], [
		#include <linux/dcache.h>
	], [
		set_default_d_op(NULL, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SET_DEFAULT_D_OP], [
	AC_MSG_CHECKING([whether set_default_d_op() is available])
	ZFS_LINUX_TEST_RESULT([set_default_d_op], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_DEFAULT_D_OP, 1,
		    [Define if set_default_d_op() is available])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_DENTRY], [
        ZFS_AC_KERNEL_SRC_D_OBTAIN_ALIAS
        ZFS_AC_KERNEL_SRC_D_SET_D_OP
        ZFS_AC_KERNEL_SRC_SET_DEFAULT_D_OP
])

AC_DEFUN([ZFS_AC_KERNEL_DENTRY], [
        ZFS_AC_KERNEL_D_OBTAIN_ALIAS
        ZFS_AC_KERNEL_D_SET_D_OP
        ZFS_AC_KERNEL_SET_DEFAULT_D_OP
])
