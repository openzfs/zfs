dnl #
dnl # Linux 4.19 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FADVISE], [
	AC_MSG_CHECKING([whether fops->fadvise() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*fadvise) (struct file *, loff_t, loff_t, int) = NULL;
		struct file_operations fops __attribute__ ((unused)) = {
			.fadvise = fadvise,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FADVISE, 1, [fops->fadvise() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
