dnl #
dnl # 3.6 API change,
dnl # 'sget' now takes the mount flags as an argument.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SGET], [
	ZFS_LINUX_TEST_SRC([sget_5args], [
		#include <linux/fs.h>
	],[
		struct file_system_type *type = NULL;
		int (*test)(struct super_block *,void *) = NULL;
		int (*set)(struct super_block *,void *) = NULL;
		int flags = 0;
		void *data = NULL;
		(void) sget(type, test, set, flags, data);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SGET], [
	AC_MSG_CHECKING([whether sget() wants 5 args])
	ZFS_LINUX_TEST_RESULT([sget_5args], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([sget()])
	])
])
