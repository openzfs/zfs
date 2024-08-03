dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_REVALIDATE_NAMEIDATA], [
	ZFS_LINUX_TEST_SRC([dentry_operations_revalidate], [
		#include <linux/dcache.h>
		#include <linux/sched.h>

		static int revalidate (struct dentry *dentry,
		    struct nameidata *nidata) { return 0; }

		static const struct dentry_operations
		    dops __attribute__ ((unused)) = {
			.d_revalidate	= revalidate,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA], [
	AC_MSG_CHECKING([whether dops->d_revalidate() takes struct nameidata])
	ZFS_LINUX_TEST_RESULT([dentry_operations_revalidate], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_REVALIDATE_NAMEIDATA, 1,
		    [dops->d_revalidate() operation takes nameidata])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_DENTRY], [
        ZFS_AC_KERNEL_SRC_D_REVALIDATE_NAMEIDATA
])

AC_DEFUN([ZFS_AC_KERNEL_DENTRY], [
        ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA
])
