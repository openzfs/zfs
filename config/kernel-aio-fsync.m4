dnl #
dnl # Linux 4.9-rc5+ ABI, removal of the .aio_fsync field
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_AIO_FSYNC], [
	ZFS_LINUX_TEST_SRC([aio_fsync], [
		#include <linux/fs.h>

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.aio_fsync = NULL,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_AIO_FSYNC], [
	AC_MSG_CHECKING([whether fops->aio_fsync() exists])
	ZFS_LINUX_TEST_RESULT([aio_fsync], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_AIO_FSYNC, 1, [fops->aio_fsync() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
