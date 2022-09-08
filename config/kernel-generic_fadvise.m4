dnl #
dnl # 5.3 API change
dnl # The generic_fadvise() function is present since 4.19 kernel
dnl # but it was not exported until Linux 5.3.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_FADVISE], [
	ZFS_LINUX_TEST_SRC([generic_fadvise], [
		#include <linux/fs.h>
	], [
		struct file *fp __attribute__ ((unused)) = NULL;
		loff_t offset __attribute__ ((unused)) = 0;
		loff_t len __attribute__ ((unused)) = 0;
		int advise __attribute__ ((unused)) = 0;
		generic_fadvise(fp, offset, len, advise);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_FADVISE], [
	AC_MSG_CHECKING([whether generic_fadvise() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([generic_fadvise],
	[generic_fadvise], [mm/fadvise.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_FADVISE, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])
