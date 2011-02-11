dnl #
dnl # 2.6.35 API change
dnl # The dentry argument was deamed unused and dropped in 2.6.36.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FSYNC_2ARGS], [
	AC_MSG_CHECKING([whether fops->fsync() wants 2 args])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*fsync) (struct file *, int datasync) = NULL;
		struct file_operations fops;

		fops.fsync = fsync;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_FSYNC, 1, [fops->fsync() want 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])
