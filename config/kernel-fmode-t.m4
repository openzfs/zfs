dnl #
dnl # 2.6.28 API change,
dnl # check if fmode_t typedef is defined
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FMODE_T], [
	ZFS_LINUX_TEST_SRC([type_fmode_t], [
		#include <linux/types.h>
	],[
		fmode_t *ptr __attribute__ ((unused));
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FMODE_T], [
	AC_MSG_CHECKING([whether kernel defines fmode_t])
	ZFS_LINUX_TEST_RESULT([type_fmode_t], [
		AC_MSG_RESULT([yes])
	],[
		ZFS_LINUX_TEST_ERROR([type_fmode_t])
	])
])
