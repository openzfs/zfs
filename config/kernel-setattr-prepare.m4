AC_DEFUN([ZFS_AC_KERNEL_SRC_SETATTR_PREPARE], [
	dnl #
	dnl # 4.9 API change
	dnl # The inode_change_ok() function has been renamed setattr_prepare()
	dnl # and updated to take a dentry rather than an inode.
	dnl #
	ZFS_LINUX_TEST_SRC([setattr_prepare], [
		#include <linux/fs.h>
	], [
		struct dentry *dentry = NULL;
		struct iattr *attr = NULL;
		int error __attribute__ ((unused)) =
			setattr_prepare(dentry, attr);
	])

	dnl #
	dnl # 5.12 API change
	dnl # The setattr_prepare() function has been changed to accept a new argument
	dnl # for struct user_namespace*
	dnl #
	ZFS_LINUX_TEST_SRC([setattr_prepare_userns], [
		#include <linux/fs.h>
	], [
		struct dentry *dentry = NULL;
		struct iattr *attr = NULL;
		struct user_namespace *userns = NULL;
		int error __attribute__ ((unused)) =
			setattr_prepare(userns, dentry, attr);
	])

	dnl #
	dnl # 6.3 API change
	dnl # The first arg of setattr_prepare() is changed to struct mnt_idmap*
	dnl #
	ZFS_LINUX_TEST_SRC([setattr_prepare_mnt_idmap], [
		#include <linux/fs.h>
	], [
		struct dentry *dentry = NULL;
		struct iattr *attr = NULL;
		struct mnt_idmap *idmap = NULL;
		int error __attribute__ ((unused)) =
			setattr_prepare(idmap, dentry, attr);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SETATTR_PREPARE], [
	AC_MSG_CHECKING([whether setattr_prepare() is available and accepts struct mnt_idmap*])
	ZFS_LINUX_TEST_RESULT_SYMBOL([setattr_prepare_mnt_idmap],
	    [setattr_prepare], [fs/attr.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SETATTR_PREPARE_IDMAP, 1,
		    [setattr_prepare() accepts mnt_idmap])
	], [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether setattr_prepare() is available and accepts struct user_namespace*])
		ZFS_LINUX_TEST_RESULT_SYMBOL([setattr_prepare_userns],
		    [setattr_prepare], [fs/attr.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SETATTR_PREPARE_USERNS, 1,
			    [setattr_prepare() accepts user_namespace])
		], [
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([whether setattr_prepare() is available, doesn't accept user_namespace])
			ZFS_LINUX_TEST_RESULT_SYMBOL([setattr_prepare],
				[setattr_prepare], [fs/attr.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_SETATTR_PREPARE_NO_USERNS, 1,
					[setattr_prepare() is available, doesn't accept user_namespace])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
