AC_DEFUN([ZFS_AC_KERNEL_SRC_RENAME], [
	dnl #
	dnl # 4.9 API change,
	dnl #
	dnl # iops->rename2() merged into iops->rename(), and iops->rename() now
	dnl # wants flags.
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_rename_flags], [
		#include <linux/fs.h>
		static int rename_fn(struct inode *sip, struct dentry *sdp,
			struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename = rename_fn,
		};
	],[])

	dnl #
	dnl # 5.12 API change,
	dnl #
	dnl # Linux 5.12 introduced passing struct user_namespace* as the first
	dnl # argument of the rename() and other inode_operations members.
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_rename_userns], [
		#include <linux/fs.h>
		static int rename_fn(struct user_namespace *user_ns, struct inode *sip,
			struct dentry *sdp, struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename = rename_fn,
		};
	],[])

	dnl #
	dnl # 6.3 API change - the first arg is now struct mnt_idmap*
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_rename_mnt_idmap], [
		#include <linux/fs.h>
		static int rename_fn(struct mnt_idmap *idmap, struct inode *sip,
			struct dentry *sdp, struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename = rename_fn,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_RENAME], [
	AC_MSG_CHECKING([whether iops->rename() takes struct mnt_idmap*])
	ZFS_LINUX_TEST_RESULT([inode_operations_rename_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOPS_RENAME_IDMAP, 1,
		    [iops->rename() takes struct mnt_idmap*])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether iops->rename() takes struct user_namespace*])
		ZFS_LINUX_TEST_RESULT([inode_operations_rename_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_IOPS_RENAME_USERNS, 1,
			    [iops->rename() takes struct user_namespace*])
		],[
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([whether iops->rename() wants flags])
			ZFS_LINUX_TEST_RESULT([inode_operations_rename_flags], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_RENAME_WANTS_FLAGS, 1,
					[iops->rename() wants flags])
			],[
				AC_MSG_RESULT(no)
			])
		])
	])
])
