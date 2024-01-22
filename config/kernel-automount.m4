dnl #
dnl # 2.6.37 API change
dnl # The dops->d_automount() dentry operation was added as a clean
dnl # solution to handling automounts.  Prior to this cifs/nfs clients
dnl # which required automount support would abuse the follow_link()
dnl # operation on directories for this purpose.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_AUTOMOUNT], [
	ZFS_LINUX_TEST_SRC([dentry_operations_d_automount], [
		#include <linux/dcache.h>
		static struct vfsmount *d_automount(struct path *p) { return NULL; }
		struct dentry_operations dops __attribute__ ((unused)) = {
			.d_automount = d_automount,
		};
	])
])

AC_DEFUN([ZFS_AC_KERNEL_AUTOMOUNT], [
	AC_MSG_CHECKING([whether dops->d_automount() exists])
	ZFS_LINUX_TEST_RESULT([dentry_operations_d_automount], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([dops->d_automount()])
	])
])
