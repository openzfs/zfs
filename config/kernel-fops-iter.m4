dnl #
dnl # Linux 4.1.x API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FOPS],
	[AC_MSG_CHECKING([whether fops have .iter*])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_read(struct kiocb *kiocb, struct iov_iter *to)
		    { return 0; }

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.read_iter = test_read,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ITER_FOPS, 1,
			[.iter_XXX fops])
	],[
		AC_MSG_RESULT(no)
	])
])
