dnl #
dnl # 5.12 API
dnl #
dnl # generic_fillattr in linux/fs.h now requires a struct user_namespace*
dnl # as the first arg, to support idmapped mounts.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_FILLATTR_USERNS], [
	ZFS_LINUX_TEST_SRC([generic_fillattr_userns], [
		#include <linux/fs.h>
	],[
		struct user_namespace *userns = NULL;
		struct inode *in = NULL;
		struct kstat *k = NULL;
		generic_fillattr(userns, in, k);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_FILLATTR_USERNS], [
	AC_MSG_CHECKING([whether generic_fillattr requires struct user_namespace*])
	ZFS_LINUX_TEST_RESULT([generic_fillattr_userns], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_GENERIC_FILLATTR_USERNS, 1,
		    [generic_fillattr requires struct user_namespace*])
	],[
		AC_MSG_RESULT([no])
	])
])

