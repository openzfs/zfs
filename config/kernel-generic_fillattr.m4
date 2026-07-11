dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 6.6 API
dnl # generic_fillattr() now takes u32 as second argument, representing a
dnl # request_mask for statx
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_FILLATTR], [
	ZFS_LINUX_TEST_SRC([generic_fillattr_reqmask], [
		#include <linux/fs.h>
	],[
		struct mnt_idmap *idmap = NULL;
		struct inode *in = NULL;
		struct kstat *k = NULL;
		generic_fillattr(idmap, 0, in, k);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_FILLATTR], [
	AC_MSG_CHECKING([whether generic_fillattr request_mask])
	ZFS_LINUX_TEST_RESULT([generic_fillattr_reqmask], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK, 1,
		    [generic_fillattr requires request_mask])
	],[
		AC_MSG_RESULT([no])
	])
])

