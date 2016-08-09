dnl #
dnl # 4.8 API change,
dnl # bio->bi_rw is renamed to bi_opf to reflect the encoding change.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_BI_OPF], [
	AC_MSG_CHECKING([whether bio->bi_opf is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		struct bio bio __attribute__ ((unused));
		bio.bi_opf = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BI_OPF, 1, [bio->bi_opf is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
