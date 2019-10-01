dnl #
dnl # 2.6.33 API change,
dnl # Removed .ctl_name from struct ctl_table.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CTL_NAME], [
	ZFS_LINUX_TEST_SRC([ctl_name], [
		#include <linux/sysctl.h>
	],[
		struct ctl_table ctl __attribute__ ((unused));
		ctl.ctl_name = 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CTL_NAME], [
	AC_MSG_CHECKING([whether struct ctl_table has ctl_name])
	ZFS_LINUX_TEST_RESULT([ctl_name], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CTL_NAME, 1, [struct ctl_table has ctl_name])
	],[
		AC_MSG_RESULT(no)
	])
])
