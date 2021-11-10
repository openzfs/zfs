dnl #
dnl # 3.11 API change
dnl # lseek_execute helper exported
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_LSEEK_EXECUTE], [
	ZFS_LINUX_TEST_SRC([lseek_execute], [
		#include <linux/fs.h>
	], [
		struct file *fp __attribute__ ((unused)) = NULL;
		struct inode *ip __attribute__ ((unused)) = NULL;
		loff_t offset __attribute__ ((unused)) = 0;
		loff_t maxsize __attribute__ ((unused)) = 0;

		lseek_execute(fp, ip, offset, maxsize);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_LSEEK_EXECUTE], [
	AC_MSG_CHECKING([whether lseek_execute() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([lseek_execute],
	    [lseek_exclusive], [fs/read_write.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LSEEK_EXECUTE, 1, [lseek_execute() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
