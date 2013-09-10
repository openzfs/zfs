dnl #
dnl # 2.6.32 API change
dnl # Private backing_device_info interfaces available
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDI], [
	AC_MSG_CHECKING([whether super_block has s_bdi])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		static const struct super_block
		    sb __attribute__ ((unused)) {
			.s_bdi = NULL,
		}
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDI, 1, [struct super_block has s_bdi])
	],[
		AC_MSG_RESULT(no)
	])
])
