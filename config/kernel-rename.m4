dnl #
dnl # 4.9 API change
dnl # The iops->rename() callback has been replaced by iops->rename2()
dnl # which was introduced in an earlier kernel release.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_RENAME_WITH_FLAGS], [
	AC_MSG_CHECKING([whether iops->rename() wants flags])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int rename(struct inode *old_dir, struct dentry *old_dentry,
		    struct inode *new_dir, struct dentry *new_dentry,
		    unsigned int flags) { return (0); }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename = rename,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_RENAME_WITH_FLAGS, 1,
			[iops->rename() wants flags])
	],[
		AC_MSG_RESULT([no])
	])
])
