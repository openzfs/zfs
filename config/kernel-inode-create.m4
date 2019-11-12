dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CREATE_FLAGS], [
	ZFS_LINUX_TEST_SRC([create_flags], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		int inode_create(struct inode *inode ,struct dentry *dentry,
		    umode_t umode, bool flag) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.create		= inode_create,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_CREATE_FLAGS], [
	AC_MSG_CHECKING([whether iops->create() passes flags])
	ZFS_LINUX_TEST_RESULT([create_flags], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([iops->create()])
	])
])
