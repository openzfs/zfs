dnl #
dnl # 4.9 API change,
dnl # iops->rename2() merged into iops->rename(), and iops->rename() now wants
dnl # flags.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_RENAME], [
	ZFS_LINUX_TEST_SRC([inode_operations_rename2], [
		#include <linux/fs.h>
		int rename2_fn(struct inode *sip, struct dentry *sdp,
			struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename2 = rename2_fn,
		};
	],[])

	ZFS_LINUX_TEST_SRC([inode_operations_rename_flags], [
		#include <linux/fs.h>
		int rename_fn(struct inode *sip, struct dentry *sdp,
			struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename = rename_fn,
		};
	],[])

	ZFS_LINUX_TEST_SRC([dir_inode_operations_wrapper_rename2], [
		#include <linux/fs.h>
		int rename2_fn(struct inode *sip, struct dentry *sdp,
			struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations_wrapper
		    iops __attribute__ ((unused)) = {
			.rename2 = rename2_fn,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_RENAME], [
	AC_MSG_CHECKING([whether iops->rename2() exists])
	ZFS_LINUX_TEST_RESULT([inode_operations_rename2], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RENAME2, 1, [iops->rename2() exists])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether iops->rename() wants flags])
		ZFS_LINUX_TEST_RESULT([inode_operations_rename_flags], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_RENAME_WANTS_FLAGS, 1,
			    [iops->rename() wants flags])
		],[
			AC_MSG_RESULT(no)

			AC_MSG_CHECKING([whether struct inode_operations_wrapper takes .rename2()])
			ZFS_LINUX_TEST_RESULT([dir_inode_operations_wrapper_rename2], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_RENAME2_OPERATIONS_WRAPPER, 1,
				    [struct inode_operations_wrapper takes .rename2()])
			],[
				AC_MSG_RESULT(no)
			])
		])
	])
])

