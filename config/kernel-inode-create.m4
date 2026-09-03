dnl # SPDX-License-Identifier: CDDL-1.0
AC_DEFUN([ZFS_AC_KERNEL_SRC_CREATE], [
        dnl #
	dnl # 7.3 API change
	dnl # bool flag parameter removed from create()
	dnl #
	ZFS_LINUX_TEST_SRC([create_mnt_idmap_noflags], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		static int inode_create(struct mnt_idmap *idmap,
		    struct inode *inode, struct dentry *dentry,
		    umode_t umode) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.create         = inode_create,
		};
	],[])

	dnl #
	dnl # 6.3 API change
	dnl # The first arg is changed to struct mnt_idmap *
	dnl #
	ZFS_LINUX_TEST_SRC([create_mnt_idmap], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		static int inode_create(struct mnt_idmap *idmap,
		    struct inode *inode ,struct dentry *dentry,
		    umode_t umode, bool flag) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.create         = inode_create,
		};
	],[])

	dnl #
	dnl # 5.12 API change that added the struct user_namespace* arg
	dnl # to the front of this function type's arg list.
	dnl #
	ZFS_LINUX_TEST_SRC([create_userns], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		static int inode_create(struct user_namespace *userns,
		    struct inode *inode ,struct dentry *dentry,
		    umode_t umode, bool flag) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.create		= inode_create,
		};
	],[])

	dnl #
	dnl # 3.6 API change
	dnl #
	ZFS_LINUX_TEST_SRC([create_flags], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		static int inode_create(struct inode *inode ,struct dentry *dentry,
		    umode_t umode, bool flag) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.create		= inode_create,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_CREATE], [
	AC_MSG_CHECKING([whether iops->create() takes struct mnt_idmap* (no flags)])
	ZFS_LINUX_TEST_RESULT([create_mnt_idmap_noflags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOPS_CREATE_IDMAP, 1,
		   [iops->create() takes struct mnt_idmap*])
		AC_DEFINE(HAVE_IOPS_CREATE_IDMAP_NOFLAGS, 1,
		   [iops->create() takes struct mnt_idmap* without flags])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether iops->create() takes struct mnt_idmap*])
		ZFS_LINUX_TEST_RESULT([create_mnt_idmap], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_IOPS_CREATE_IDMAP, 1,
			   [iops->create() takes struct mnt_idmap*])
		],[
			...existing cascade unchanged...
		])
	])
])
