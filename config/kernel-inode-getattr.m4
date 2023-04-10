AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_GETATTR], [
	dnl #
	dnl # Linux 6.3 API
	dnl # The first arg of getattr I/O operations handler type
	dnl # is changed to struct mnt_idmap*
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_getattr_mnt_idmap], [
		#include <linux/fs.h>

		int test_getattr(
		    struct mnt_idmap *idmap,
		    const struct path *p, struct kstat *k,
		    u32 request_mask, unsigned int query_flags)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.getattr = test_getattr,
		};
	],[])

	dnl #
	dnl # Linux 5.12 API
	dnl # The getattr I/O operations handler type was extended to require
	dnl # a struct user_namespace* as its first arg, to support idmapped
	dnl # mounts.
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_getattr_userns], [
		#include <linux/fs.h>

		int test_getattr(
			struct user_namespace *userns,
		    const struct path *p, struct kstat *k,
		    u32 request_mask, unsigned int query_flags)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.getattr = test_getattr,
		};
	],[])

	dnl #
	dnl # Linux 4.11 API
	dnl # See torvalds/linux@a528d35
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_getattr_path], [
		#include <linux/fs.h>

		int test_getattr(
		    const struct path *p, struct kstat *k,
		    u32 request_mask, unsigned int query_flags)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.getattr = test_getattr,
		};
	],[])

	ZFS_LINUX_TEST_SRC([inode_operations_getattr_vfsmount], [
		#include <linux/fs.h>

		int test_getattr(
		    struct vfsmount *mnt, struct dentry *d,
		    struct kstat *k)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.getattr = test_getattr,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_GETATTR], [
	dnl #
	dnl # Kernel 6.3 test
	dnl #
	AC_MSG_CHECKING([whether iops->getattr() takes mnt_idmap])
	ZFS_LINUX_TEST_RESULT([inode_operations_getattr_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IDMAP_IOPS_GETATTR, 1,
		    [iops->getattr() takes struct mnt_idmap*])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # Kernel 5.12 test
		dnl #
		AC_MSG_CHECKING([whether iops->getattr() takes user_namespace])
		ZFS_LINUX_TEST_RESULT([inode_operations_getattr_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_USERNS_IOPS_GETATTR, 1,
			    [iops->getattr() takes struct user_namespace*])
		],[
			AC_MSG_RESULT(no)

			dnl #
			dnl # Kernel 4.11 test
			dnl #
			AC_MSG_CHECKING([whether iops->getattr() takes a path])
			ZFS_LINUX_TEST_RESULT([inode_operations_getattr_path], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_PATH_IOPS_GETATTR, 1,
					[iops->getattr() takes a path])
			],[
				AC_MSG_RESULT(no)

				dnl #
				dnl # Kernel < 4.11 test
				dnl #
				AC_MSG_CHECKING([whether iops->getattr() takes a vfsmount])
				ZFS_LINUX_TEST_RESULT([inode_operations_getattr_vfsmount], [
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_VFSMOUNT_IOPS_GETATTR, 1,
						[iops->getattr() takes a vfsmount])
				],[
					AC_MSG_RESULT(no)
				])
			])
		])
	])
])
