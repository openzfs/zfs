dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_LOOKUP_FLAGS], [
	ZFS_LINUX_TEST_SRC([lookup_flags], [
		#include <linux/fs.h>
		#include <linux/sched.h>

		struct dentry *inode_lookup(struct inode *inode,
		    struct dentry *dentry, unsigned int flags) { return NULL; }

		static const struct inode_operations iops
		    __attribute__ ((unused)) = {
			.lookup	= inode_lookup,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_LOOKUP_FLAGS], [
	AC_MSG_CHECKING([whether iops->lookup() passes flags])
	ZFS_LINUX_TEST_RESULT([lookup_flags], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([iops->lookup()])
	])
])
