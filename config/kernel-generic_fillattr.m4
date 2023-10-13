dnl #
dnl # 5.12 API
dnl #
dnl # generic_fillattr in linux/fs.h now requires a struct user_namespace*
dnl # as the first arg, to support idmapped mounts.
dnl #
dnl # 6.3 API
dnl # generic_fillattr() now takes struct mnt_idmap* as the first argument
dnl #
dnl # 6.6 API
dnl # generic_fillattr() now takes u32 as second argument, representing a
dnl # request_mask for statx
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_FILLATTR], [
	ZFS_LINUX_TEST_SRC([generic_fillattr_userns], [
		#include <linux/fs.h>
	],[
		struct user_namespace *userns = NULL;
		struct inode *in = NULL;
		struct kstat *k = NULL;
		generic_fillattr(userns, in, k);
	])

	ZFS_LINUX_TEST_SRC([generic_fillattr_mnt_idmap], [
		#include <linux/fs.h>
	],[
		struct mnt_idmap *idmap = NULL;
		struct inode *in = NULL;
		struct kstat *k = NULL;
		generic_fillattr(idmap, in, k);
	])

	ZFS_LINUX_TEST_SRC([generic_fillattr_mnt_idmap_reqmask], [
		#include <linux/fs.h>
	],[
		struct mnt_idmap *idmap = NULL;
		struct inode *in = NULL;
		struct kstat *k = NULL;
		generic_fillattr(idmap, 0, in, k);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_FILLATTR], [
	AC_MSG_CHECKING(
	    [whether generic_fillattr requires struct mnt_idmap* and request_mask])
	ZFS_LINUX_TEST_RESULT([generic_fillattr_mnt_idmap_reqmask], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_GENERIC_FILLATTR_IDMAP_REQMASK, 1,
		    [generic_fillattr requires struct mnt_idmap* and u32 request_mask])
	],[
		AC_MSG_CHECKING([whether generic_fillattr requires struct mnt_idmap*])
		ZFS_LINUX_TEST_RESULT([generic_fillattr_mnt_idmap], [
			AC_MSG_RESULT([yes])
			AC_DEFINE(HAVE_GENERIC_FILLATTR_IDMAP, 1,
				[generic_fillattr requires struct mnt_idmap*])
		],[
			AC_MSG_CHECKING([whether generic_fillattr requires struct user_namespace*])
			ZFS_LINUX_TEST_RESULT([generic_fillattr_userns], [
				AC_MSG_RESULT([yes])
				AC_DEFINE(HAVE_GENERIC_FILLATTR_USERNS, 1,
					[generic_fillattr requires struct user_namespace*])
			],[
				AC_MSG_RESULT([no])
			])
		])
	])
])

