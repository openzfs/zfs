dnl #
dnl # Linux 6.5 removes register_sysctl_table
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_REGISTER_SYSCTL_TABLE], [
	ZFS_LINUX_TEST_SRC([has_register_sysctl_table], [
		#include <linux/sysctl.h>

		static struct ctl_table dummy_table[] = {
			{}
		};

    ],[
		struct ctl_table_header *h
			__attribute((unused)) = register_sysctl_table(dummy_table);
    ])
])

AC_DEFUN([ZFS_AC_KERNEL_REGISTER_SYSCTL_TABLE], [
	AC_MSG_CHECKING([whether register_sysctl_table exists])
	ZFS_LINUX_TEST_RESULT([has_register_sysctl_table], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_REGISTER_SYSCTL_TABLE, 1,
			[register_sysctl_table exists])
	],[
		AC_MSG_RESULT([no])
	])
])
