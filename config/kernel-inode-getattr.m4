dnl #
dnl # Linux 4.11 API
dnl # See torvalds/linux@a528d35
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_GETATTR], [
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
	AC_MSG_CHECKING([whether iops->getattr() takes a path])
	ZFS_LINUX_TEST_RESULT([inode_operations_getattr_path], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PATH_IOPS_GETATTR, 1,
		    [iops->getattr() takes a path])
	],[
		AC_MSG_RESULT(no)

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
