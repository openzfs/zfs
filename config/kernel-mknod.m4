AC_DEFUN([ZFS_AC_KERNEL_SRC_MKNOD], [
	dnl #
	dnl # 6.3 API change
	dnl # The first arg is now struct mnt_idmap*
	dnl #
	ZFS_LINUX_TEST_SRC([mknod_mnt_idmap], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		int tmp_mknod(struct mnt_idmap *idmap,
		    struct inode *inode ,struct dentry *dentry,
		    umode_t u, dev_t d) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.mknod          = tmp_mknod,
		};
	],[])

	dnl #
	dnl # 5.12 API change that added the struct user_namespace* arg
	dnl # to the front of this function type's arg list.
	dnl #
	ZFS_LINUX_TEST_SRC([mknod_userns], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		int tmp_mknod(struct user_namespace *userns,
		    struct inode *inode ,struct dentry *dentry,
		    umode_t u, dev_t d) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.mknod		= tmp_mknod,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_MKNOD], [
	AC_MSG_CHECKING([whether iops->mknod() takes struct mnt_idmap*])
	ZFS_LINUX_TEST_RESULT([mknod_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOPS_MKNOD_IDMAP, 1,
		    [iops->mknod() takes struct mnt_idmap*])
	],[
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether iops->mknod() takes struct user_namespace*])
		ZFS_LINUX_TEST_RESULT([mknod_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_IOPS_MKNOD_USERNS, 1,
			    [iops->mknod() takes struct user_namespace*])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
