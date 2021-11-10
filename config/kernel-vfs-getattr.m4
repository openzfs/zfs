dnl #
dnl # 4.11 API, a528d35e@torvalds/linux
dnl # vfs_getattr(const struct path *p, struct kstat *s, u32 m, unsigned int f)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_GETATTR_4ARGS], [
	ZFS_LINUX_TEST_SRC([vfs_getattr_4args], [
		#include <linux/fs.h>
	],[
		vfs_getattr((const struct path *)NULL,
			(struct kstat *)NULL,
			(u32)0,
			(unsigned int)0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_GETATTR_4ARGS], [
	AC_MSG_CHECKING([whether vfs_getattr() wants 4 args])
	ZFS_LINUX_TEST_RESULT([vfs_getattr_4args], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_4ARGS_VFS_GETATTR, 1,
		    [vfs_getattr wants 4 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.9 API
dnl # vfs_getattr(struct path *p, struct kstat *s)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_GETATTR_2ARGS], [
	ZFS_LINUX_TEST_SRC([vfs_getattr_2args], [
		#include <linux/fs.h>
	],[
		vfs_getattr((struct path *) NULL,
			(struct kstat *)NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_GETATTR_2ARGS], [
	AC_MSG_CHECKING([whether vfs_getattr() wants 2 args])
	ZFS_LINUX_TEST_RESULT([vfs_getattr_2args], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_GETATTR, 1,
		    [vfs_getattr wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # <3.9 API
dnl # vfs_getattr(struct vfsmount *v, struct dentry *d, struct kstat *k)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_GETATTR_3ARGS], [
	ZFS_LINUX_TEST_SRC([vfs_getattr_3args], [
		#include <linux/fs.h>
	],[
		vfs_getattr((struct vfsmount *)NULL,
			(struct dentry *)NULL,
			(struct kstat *)NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_GETATTR_3ARGS], [
	AC_MSG_CHECKING([whether vfs_getattr() wants 3 args])
	ZFS_LINUX_TEST_RESULT([vfs_getattr_3args], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_VFS_GETATTR, 1,
		    [vfs_getattr wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_GETATTR], [
	ZFS_AC_KERNEL_SRC_VFS_GETATTR_4ARGS
	ZFS_AC_KERNEL_SRC_VFS_GETATTR_2ARGS
	ZFS_AC_KERNEL_SRC_VFS_GETATTR_3ARGS
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_GETATTR], [
	ZFS_AC_KERNEL_VFS_GETATTR_4ARGS
	ZFS_AC_KERNEL_VFS_GETATTR_2ARGS
	ZFS_AC_KERNEL_VFS_GETATTR_3ARGS
])
