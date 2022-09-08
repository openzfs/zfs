dnl #
dnl # Linux 4.19 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FADVISE], [
	ZFS_LINUX_TEST_SRC([file_fadvise], [
		#include <linux/fs.h>

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.fadvise = NULL,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_FADVISE], [
	AC_MSG_CHECKING([whether fops->fadvise() exists])
	ZFS_LINUX_TEST_RESULT([file_fadvise], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_FADVISE, 1, [fops->fadvise() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
