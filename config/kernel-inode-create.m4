dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 7.3 API change.
dnl #
dnl # inode_operations->create drops flag arg. We test for the mnt_idmap
dnl # version without the flag arg, to avoid have to test for all the
dnl # idmap/userns variants back to before 5.12 (see kernel-idmap.m4).
dnl #
dnl # 7.4 API change.
dnl #
dnl # mnt_idmap is passed to inode ops as const, so we need a second check
dnl # with a const prototype.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_IOPS_CREATE_NO_FLAG], [
	ZFS_LINUX_TEST_SRC([iops_create_no_flag], [
		#include <linux/fs.h>

		static int test_iops_create(struct mnt_idmap *idmap,
		    struct inode *inode, struct dentry *dentry,
		    umode_t mode) { return 0; }

		static const struct inode_operations test_iops
		    __attribute__ ((unused)) = {
			.create = test_iops_create
		};
	],[])
	ZFS_LINUX_TEST_SRC([iops_create_no_flag_const], [
		#include <linux/fs.h>

		static int test_iops_create(const struct mnt_idmap *idmap,
		    struct inode *inode, struct dentry *dentry,
		    umode_t mode) { return 0; }

		static const struct inode_operations test_iops
		    __attribute__ ((unused)) = {
			.create = test_iops_create
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_IOPS_CREATE_NO_FLAG], [
	AC_MSG_CHECKING([whether iops->create() takes a flag arg])
	_result=no
	ZFS_LINUX_TEST_RESULT([iops_create_no_flag], [_result=yes])
	ZFS_LINUX_TEST_RESULT([iops_create_no_flag_const], [_result=yes])
	AS_IF([test "x$_result" = "xyes"], [
		AC_MSG_RESULT(no)
		AC_DEFINE(HAVE_IOPS_CREATE_NO_FLAG_ARG, 1,
		    [iops->create() does not take a flag arg])
	],[
		AC_MSG_RESULT(yes)
	])
])

