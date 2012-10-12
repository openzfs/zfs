dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CREATE_NAMEIDATA], [
	AC_MSG_CHECKING([whether iops->create() takes struct nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*inode_create) (struct inode *,struct dentry *,
		                     umode_t, struct nameidata *) = NULL;
		struct inode_operations iops __attribute__ ((unused)) = {
			.create		= inode_create,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_NAMEIDATA, 1,
		          [iops->create() operation takes nameidata])
	],[
		AC_MSG_RESULT(no)
	])
])
