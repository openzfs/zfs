dnl #
dnl # 2.6.37 API change
dnl # The dops->d_automount() dentry operation was added as a clean
dnl # solution to handling automounts.  Prior to this cifs/nfs clients
dnl # which required automount support would abuse the follow_link()
dnl # operation on directories for this purpose.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_AUTOMOUNT], [
	ZFS_LINUX_TEST_SRC([dentry_operations_d_automount], [
		#include <linux/dcache.h>
		static struct vfsmount *d_automount(struct path *p) { return NULL; }
		struct dentry_operations dops __attribute__ ((unused)) = {
			.d_automount = d_automount,
		};
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_AUTOMOUNT], [
	AC_MSG_CHECKING([whether dops->d_automount() exists])
	ZFS_LINUX_TEST_RESULT([dentry_operations_d_automount], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([dops->d_automount()])
	])
])

dnl #
dnl # 6.14 API change
dnl # dops->d_revalidate now has four args.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_REVALIDATE_4ARGS], [
	ZFS_LINUX_TEST_SRC([dentry_operations_d_revalidate_4args], [
		#include <linux/dcache.h>
		static int d_revalidate(struct inode *dir,
		    const struct qstr *name, struct dentry *dentry,
		    unsigned int fl) { return 0; }
		struct dentry_operations dops __attribute__ ((unused)) = {
			.d_revalidate = d_revalidate,
		};
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_REVALIDATE_4ARGS], [
	AC_MSG_CHECKING([whether dops->d_revalidate() takes 4 args])
	ZFS_LINUX_TEST_RESULT([dentry_operations_d_revalidate_4args], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_REVALIDATE_4ARGS, 1,
		    [dops->d_revalidate() takes 4 args])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_AUTOMOUNT], [
	ZFS_AC_KERNEL_SRC_D_AUTOMOUNT
	ZFS_AC_KERNEL_SRC_D_REVALIDATE_4ARGS
])

AC_DEFUN([ZFS_AC_KERNEL_AUTOMOUNT], [
	ZFS_AC_KERNEL_D_AUTOMOUNT
	ZFS_AC_KERNEL_D_REVALIDATE_4ARGS
])
