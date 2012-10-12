dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_WRITE_SUPER], [
	AC_MSG_CHECKING([whether sops->write_super() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct super_operations sops __attribute__ ((unused)) = {
			.write_super = NULL
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WRITE_SUPRT, 1,
			[sops->write_super() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
