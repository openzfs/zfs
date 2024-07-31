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

dnl #
dnl # Linux 6.11 makes const the ctl_table arg of proc_handler
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PROC_HANDLER_CTL_TABLE_CONST], [
	ZFS_LINUX_TEST_SRC([has_proc_handler_ctl_table_const], [
		#include <linux/sysctl.h>

		static int test_handler(
		    const struct ctl_table *ctl __attribute((unused)),
		    int write __attribute((unused)),
		    void *buffer __attribute((unused)),
		    size_t *lenp __attribute((unused)),
		    loff_t *ppos __attribute((unused)))
		{
			return (0);
		}
	], [
		proc_handler *ph __attribute((unused)) =
		    &test_handler;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PROC_HANDLER_CTL_TABLE_CONST], [
	AC_MSG_CHECKING([whether proc_handler ctl_table arg is const])
	ZFS_LINUX_TEST_RESULT([has_proc_handler_ctl_table_const], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_PROC_HANDLER_CTL_TABLE_CONST, 1,
		    [proc_handler ctl_table arg is const])
	], [
		AC_MSG_RESULT([no])
	])
])
