dnl #
dnl # 3.13 API change
dnl # vfs_unlink() updated to take a third delegated_inode argument.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_UNLINK],
	[AC_MSG_CHECKING([whether vfs_unlink() wants 2 args])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_unlink((struct inode *) NULL, (struct dentry *) NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_UNLINK, 1,
		          [vfs_unlink() wants 2 args])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # Linux 3.13 API change
		dnl # Added delegated inode
		dnl #
		AC_MSG_CHECKING([whether vfs_unlink() wants 3 args])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
		],[
			vfs_unlink((struct inode *) NULL,
				(struct dentry *) NULL,
				(struct inode **) NULL);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_3ARGS_VFS_UNLINK, 1,
				  [vfs_unlink() wants 3 args])
		],[
			AC_MSG_ERROR(no)
		])

	])
])
