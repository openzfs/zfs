dnl # SPDX-License-Identifier: CDDL-1.0
AC_DEFUN([ZFS_AC_KERNEL_SRC_IDMAP], [
	dnl #
	dnl # 6.3 API change
	dnl # The first arg is changed to struct mnt_idmap *
	dnl #
	ZFS_LINUX_TEST_SRC([generic_permission_idmap_mntidmap], [
		#include <linux/fs.h>
	], [
		struct mnt_idmap *idmap = NULL;
		struct inode *inode = NULL;
		int mask = 0;
		int ret = generic_permission(idmap, inode, mask);
		(void) ret;
	])

	dnl #
	dnl # 5.12 API change that added the struct user_namespace* arg
	dnl # to the front of this function type's arg list.
	dnl #
	ZFS_LINUX_TEST_SRC([generic_permission_idmap_userns], [
		#include <linux/fs.h>
	], [
		struct user_namespace *userns = NULL;
		struct inode *inode = NULL;
		int mask = 0;
		int ret = generic_permission(userns, inode, mask);
		(void) ret;
	])

	dnl #
	dnl # Before 5.12 user namespace was part of kcred
	dnl #
	ZFS_LINUX_TEST_SRC([generic_permission_idmap_none], [
		#include <linux/fs.h>
	], [
		struct inode *inode = NULL;
		int mask = 0;
		int ret = generic_permission(inode, mask);
		(void) ret;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_IDMAP], [
	AC_MSG_CHECKING([for id mapping mechanism])
	ZFS_LINUX_TEST_RESULT([generic_permission_idmap_mntidmap], [
		AC_MSG_RESULT([mnt_idmap])
		AC_DEFINE(HAVE_IDMAP_MNTIDMAP, 1,
		    [id mapping mechanism is mnt_idmap])
	],[
		ZFS_LINUX_TEST_RESULT([generic_permission_idmap_userns], [
			AC_MSG_RESULT([user_namespace])
			AC_DEFINE(HAVE_IDMAP_USERNS, 1,
			    [id mapping mechanism is user_namespace])
		],[
			ZFS_LINUX_TEST_RESULT([generic_permission_idmap_none], [
				AC_MSG_RESULT([none])
			],[
				AC_MSG_RESULT([unknown])
				ZFS_LINUX_TEST_ERROR([generic_permission])
			])
		])
	])
])
