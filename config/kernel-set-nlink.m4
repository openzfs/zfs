dnl #
dnl # Linux 3.2 API change
dnl # set_nlink()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SET_NLINK], [
	ZFS_LINUX_TEST_SRC([set_nlink], [
		#include <linux/fs.h>
	],[
		struct inode node;
		unsigned int link = 0;
		(void) set_nlink(&node, link);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SET_NLINK], [
	AC_MSG_CHECKING([whether set_nlink() is available])
	ZFS_LINUX_TEST_RESULT([set_nlink], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([set_nlink()])
	])
])
