dnl #
dnl # 3.9 API change,
dnl # vfs_getattr() uses 2 args
dnl # It takes struct path * instead of struct vfsmount * and struct dentry *
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_GETATTR], [
	AC_MSG_CHECKING([whether vfs_getattr() wants])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_getattr((struct path *) NULL,
			(struct kstat *)NULL);
	],[
		AC_MSG_RESULT(2 args)
		AC_DEFINE(HAVE_2ARGS_VFS_GETATTR, 1,
		          [vfs_getattr wants 2 args])
	],[
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/fs.h>
		],[
			vfs_getattr((struct vfsmount *)NULL,
				(struct dentry *)NULL,
				(struct kstat *)NULL);
		],[
			AC_MSG_RESULT(3 args)
		],[
			AC_MSG_ERROR(unknown)
		])
	])
])
