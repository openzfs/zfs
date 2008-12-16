dnl #
dnl # 2.6.x API change
dnl # bio_end_io_t uses 2 args (size was dropped from prototype)
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL_BIO_ARGS],
	[AC_MSG_CHECKING([whether bio_end_io_t wants 2 args])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
	],[
		void (*wanted_end_io)(struct bio *, int) = NULL;
		bio_end_io_t *local_end_io;

		local_end_io = wanted_end_io;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_BIO_END_IO_T, 1,
		          [bio_end_io_t wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
