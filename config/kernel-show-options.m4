dnl #
dnl # Linux 3.3 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SHOW_OPTIONS], [
	ZFS_LINUX_TEST_SRC([super_operations_show_options], [
		#include <linux/fs.h>

		static int show_options(struct seq_file * x, struct dentry * y) {
			return 0;
		};

		static struct super_operations sops __attribute__ ((unused)) = {
			.show_options = show_options,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SHOW_OPTIONS], [
	AC_MSG_CHECKING([whether sops->show_options() wants dentry])
	ZFS_LINUX_TEST_RESULT([super_operations_show_options], [
		AC_MSG_RESULT([yes])
	],[
		ZFS_LINUX_TEST_ERROR([sops->show_options()])
	])
])
