dnl #
dnl # 2.6.24 API change
dnl # Empty write barriers are now supported and we should use them.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_EMPTY_BARRIER], [
	AC_MSG_CHECKING([whether bio_empty_barrier() is defined])
	EXTRA_KCFLAGS="-Werror"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		struct bio bio;
		(void)bio_empty_barrier(&bio);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_EMPTY_BARRIER, 1,
		          [bio_empy_barrier() is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
