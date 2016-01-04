dnl #
dnl # Linux 4.1.x API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_RW_ITERATE],
	[AC_MSG_CHECKING([whether fops->read/write_iter() are available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_read(struct kiocb *kiocb, struct iov_iter *to)
		    { return 0; }
		ssize_t test_write(struct kiocb *kiocb, struct iov_iter *from)
		    { return 0; }

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
		    .read_iter = test_read,
		    .write_iter = test_write,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_RW_ITERATE, 1,
			[fops->read/write_iter() are available])
	],[
		AC_MSG_RESULT(no)
	])
])
