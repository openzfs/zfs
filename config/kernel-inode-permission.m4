AC_DEFUN([ZFS_AC_KERNEL_SRC_PERMISSION], [
	dnl #
	dnl # 6.3 API change
	dnl # iops->permission() now takes struct mnt_idmap*
	dnl # as its first arg
	dnl #
	ZFS_LINUX_TEST_SRC([permission_mnt_idmap], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		int inode_permission(struct mnt_idmap *idmap,
		    struct inode *inode, int mask) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.permission             = inode_permission,
		};
	],[])

	dnl #
	dnl # 5.12 API change that added the struct user_namespace* arg
	dnl # to the front of this function type's arg list.
	dnl #
	ZFS_LINUX_TEST_SRC([permission_userns], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		int inode_permission(struct user_namespace *userns,
		    struct inode *inode, int mask) { return 0; }

		static const struct inode_operations
			iops __attribute__ ((unused)) = {
			.permission		= inode_permission,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_PERMISSION], [
	AC_MSG_CHECKING([whether iops->permission() takes struct mnt_idmap*])
	ZFS_LINUX_TEST_RESULT([permission_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOPS_PERMISSION_IDMAP, 1,
		   [iops->permission() takes struct mnt_idmap*])
	],[
		AC_MSG_CHECKING([whether iops->permission() takes struct user_namespace*])
		ZFS_LINUX_TEST_RESULT([permission_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_IOPS_PERMISSION_USERNS, 1,
			   [iops->permission() takes struct user_namespace*])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
