dnl #
dnl # Linux 4.1 API
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_NEW_SYNC_RW], [
	ZFS_LINUX_TEST_SRC([new_sync_rw], [
		#include <linux/fs.h>
	],[
	        ssize_t ret __attribute__ ((unused));
		struct file *filp = NULL;
		char __user *rbuf = NULL;
		const char __user *wbuf = NULL;
		size_t len = 0;
		loff_t ppos;

		ret = new_sync_read(filp, rbuf, len, &ppos);
		ret = new_sync_write(filp, wbuf, len, &ppos);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_NEW_SYNC_RW], [
	AC_MSG_CHECKING([whether new_sync_read/write() are available])
	ZFS_LINUX_TEST_RESULT([new_sync_rw], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NEW_SYNC_READ, 1,
		    [new_sync_read()/new_sync_write() are available])
	],[
		AC_MSG_RESULT(no)
	])
])
