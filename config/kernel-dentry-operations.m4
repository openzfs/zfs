dnl #
dnl # 3.4.0 API change
dnl # Added d_make_root() to replace previous d_alloc_root() function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_MAKE_ROOT], [
	ZFS_LINUX_TEST_SRC([d_make_root], [
		#include <linux/dcache.h>
	], [
		d_make_root(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_MAKE_ROOT], [
	AC_MSG_CHECKING([whether d_make_root() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([d_make_root],
	    [d_make_root], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_MAKE_ROOT, 1, [d_make_root() is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.12 API change
dnl # d_prune_aliases() helper function available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_PRUNE_ALIASES], [
	ZFS_LINUX_TEST_SRC([d_prune_aliases], [
		#include <linux/dcache.h>
	], [
		struct inode *ip = NULL;
		d_prune_aliases(ip);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_PRUNE_ALIASES], [
	AC_MSG_CHECKING([whether d_prune_aliases() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([d_prune_aliases],
	    [d_prune_aliases], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_PRUNE_ALIASES, 1,
		    [d_prune_aliases() is available])
	], [
		ZFS_LINUX_TEST_ERROR([d_prune_aliases()])
	])
])

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
        ZFS_AC_KERNEL_SRC_D_MAKE_ROOT
        ZFS_AC_KERNEL_SRC_D_PRUNE_ALIASES
        ZFS_AC_KERNEL_SRC_D_REVALIDATE_NAMEIDATA
])

AC_DEFUN([ZFS_AC_KERNEL_DENTRY], [
        ZFS_AC_KERNEL_D_MAKE_ROOT
        ZFS_AC_KERNEL_D_PRUNE_ALIASES
        ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA
])
