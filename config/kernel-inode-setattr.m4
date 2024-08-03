AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_SETATTR], [
	dnl #
	dnl # Linux 6.3 API
	dnl # The first arg of setattr I/O operations handler type
	dnl # is changed to struct mnt_idmap*
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_setattr_mnt_idmap], [
		#include <linux/fs.h>

		static int test_setattr(
		    struct mnt_idmap *idmap,
		    struct dentry *de, struct iattr *ia)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.setattr = test_setattr,
		};
	],[])

	dnl #
	dnl # Linux 5.12 API
	dnl # The setattr I/O operations handler type was extended to require
	dnl # a struct user_namespace* as its first arg, to support idmapped
	dnl # mounts.
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_setattr_userns], [
		#include <linux/fs.h>

		static int test_setattr(
		    struct user_namespace *userns,
		    struct dentry *de, struct iattr *ia)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.setattr = test_setattr,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_SETATTR], [
	dnl #
	dnl # Kernel 6.3 test
	dnl #
	AC_MSG_CHECKING([whether iops->setattr() takes mnt_idmap])
	ZFS_LINUX_TEST_RESULT([inode_operations_setattr_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IDMAP_IOPS_SETATTR, 1,
		    [iops->setattr() takes struct mnt_idmap*])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # Kernel 5.12 test
		dnl #
		AC_MSG_CHECKING([whether iops->setattr() takes user_namespace])
		ZFS_LINUX_TEST_RESULT([inode_operations_setattr_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_USERNS_IOPS_SETATTR, 1,
			    [iops->setattr() takes struct user_namespace*])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
