dnl # SPDX-License-Identifier: CDDL-1.0
AC_DEFUN([ZFS_AC_KERNEL_SRC_MKDIR], [
	dnl #
	dnl # 6.15 API change
	dnl # mkdir() returns struct dentry *
	dnl
	dnl # 7.4 API change
	dnl # mnt_idmap is passed to inode ops as const, so we need a second
	dnl # check with a const prototype.
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
	ZFS_LINUX_TEST_SRC([mkdir_return_dentry_return_const], [
		#include <linux/fs.h>

		static struct dentry *mkdir(const struct mnt_idmap *idmap,
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
	_result=no
	ZFS_LINUX_TEST_RESULT([mkdir_return_dentry_return], [_result=yes])
	ZFS_LINUX_TEST_RESULT([mkdir_return_dentry_return_const], [_result=yes])
	AS_IF([test "x$_result" = "xyes"], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MKDIR_DENTRY_RETURN, 1,
		    [iops->mkdir() returns struct dentry*])
	],[
		AC_MSG_RESULT(no)
	])
])
