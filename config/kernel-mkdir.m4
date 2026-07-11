dnl # SPDX-License-Identifier: CDDL-1.0
AC_DEFUN([ZFS_AC_KERNEL_SRC_MKDIR], [
	dnl #
	dnl # 6.15 API change
	dnl # mkdir() returns struct dentry *
	dnl #
	ZFS_LINUX_TEST_SRC([mkdir_return_dentry_return], [
		#include <linux/fs.h>

		static struct dentry *mkdir(struct mnt_idmap *idmap,
			struct inode *inode, struct dentry *dentry,
			umode_t umode) { return dentry; }
		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.mkdir = mkdir,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_MKDIR], [
	dnl #
	dnl # 6.15 API change
	dnl # mkdir() returns struct dentry *
	dnl #
	AC_MSG_CHECKING([whether iops->mkdir() returns struct dentry*])
	ZFS_LINUX_TEST_RESULT([mkdir_return_dentry_return], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MKDIR_DENTRY_RETURN, 1,
		    [iops->mkdir() returns struct dentry*])
	],[
		AC_MSG_RESULT(no)
	])
])
