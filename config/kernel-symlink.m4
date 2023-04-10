AC_DEFUN([ZFS_AC_KERNEL_SRC_SYMLINK], [
	dnl #
	dnl # 6.3 API change that changed the first arg
	dnl # to struct mnt_idmap*
	dnl #
	ZFS_LINUX_TEST_SRC([symlink_mnt_idmap], [
		#include <linux/fs.h>
		#include <linux/sched.h>
		int tmp_symlink(struct mnt_idmap *idmap,
		    struct inode *inode ,struct dentry *dentry,
		    const char *path) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.symlink                = tmp_symlink,
		};
	],[])
	dnl #
	dnl # 5.12 API change that added the struct user_namespace* arg
	dnl # to the front of this function type's arg list.
	dnl #
	ZFS_LINUX_TEST_SRC([symlink_userns], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		int tmp_symlink(struct user_namespace *userns,
		    struct inode *inode ,struct dentry *dentry,
		    const char *path) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.symlink		= tmp_symlink,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SYMLINK], [
	AC_MSG_CHECKING([whether iops->symlink() takes struct mnt_idmap*])
	ZFS_LINUX_TEST_RESULT([symlink_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOPS_SYMLINK_IDMAP, 1,
		    [iops->symlink() takes struct mnt_idmap*])
	],[
		AC_MSG_CHECKING([whether iops->symlink() takes struct user_namespace*])
		ZFS_LINUX_TEST_RESULT([symlink_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_IOPS_SYMLINK_USERNS, 1,
			    [iops->symlink() takes struct user_namespace*])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
