dnl #
dnl # PaX Linux 2.6.38 - 3.x API
dnl #
AC_DEFUN([ZFS_AC_PAX_KERNEL_FILE_FALLOCATE], [
	AC_MSG_CHECKING([whether fops->fallocate() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		long (*fallocate) (struct file *, int, loff_t, loff_t) = NULL;
		struct file_operations_no_const fops __attribute__ ((unused)) = {
			.fallocate = fallocate,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_FALLOCATE, 1, [fops->fallocate() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
