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
dnl # 2.6.28 API change
dnl # Added d_obtain_alias() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_OBTAIN_ALIAS], [
	ZFS_LINUX_TEST_SRC([d_obtain_alias], [
		#include <linux/dcache.h>
	], [
		d_obtain_alias(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_OBTAIN_ALIAS], [
	AC_MSG_CHECKING([whether d_obtain_alias() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([d_obtain_alias],
	    [d_obtain_alias], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([d_obtain_alias()])
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
dnl # 2.6.38 API change
dnl # Added d_set_d_op() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_D_SET_D_OP], [
	ZFS_LINUX_TEST_SRC([d_set_d_op], [
		#include <linux/dcache.h>
	], [
		d_set_d_op(NULL, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_D_SET_D_OP], [
	AC_MSG_CHECKING([whether d_set_d_op() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([d_set_d_op],
	    [d_set_d_op], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([d_set_d_op])
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

dnl #
dnl # 2.6.30 API change
dnl # The 'struct dentry_operations' was constified in the dentry structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONST_DENTRY_OPERATIONS], [
	ZFS_LINUX_TEST_SRC([dentry_operations_const], [
		#include <linux/dcache.h>

		const struct dentry_operations test_d_op = {
			.d_revalidate = NULL,
		};
	],[
		struct dentry d __attribute__ ((unused));
		d.d_op = &test_d_op;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CONST_DENTRY_OPERATIONS], [
	AC_MSG_CHECKING([whether dentry uses const struct dentry_operations])
	ZFS_LINUX_TEST_RESULT([dentry_operations_const], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CONST_DENTRY_OPERATIONS, 1,
		    [dentry uses const struct dentry_operations])
	],[
		ZFS_LINUX_TEST_ERROR([const dentry_operations])
	])
])

dnl #
dnl # 2.6.38 API change
dnl # Added sb->s_d_op default dentry_operations member
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_S_D_OP], [
	ZFS_LINUX_TEST_SRC([super_block_s_d_op], [
		#include <linux/fs.h>
	],[
		struct super_block sb __attribute__ ((unused));
		sb.s_d_op = NULL;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_S_D_OP], [
	AC_MSG_CHECKING([whether super_block has s_d_op])
	ZFS_LINUX_TEST_RESULT([super_block_s_d_op], [
		AC_MSG_RESULT(yes)
	], [
		ZFS_LINUX_TEST_ERROR([super_block s_d_op])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_DENTRY], [
        ZFS_AC_KERNEL_SRC_D_MAKE_ROOT
        ZFS_AC_KERNEL_SRC_D_OBTAIN_ALIAS
        ZFS_AC_KERNEL_SRC_D_PRUNE_ALIASES
        ZFS_AC_KERNEL_SRC_D_SET_D_OP
        ZFS_AC_KERNEL_SRC_D_REVALIDATE_NAMEIDATA
        ZFS_AC_KERNEL_SRC_CONST_DENTRY_OPERATIONS
        ZFS_AC_KERNEL_SRC_S_D_OP
])

AC_DEFUN([ZFS_AC_KERNEL_DENTRY], [
        ZFS_AC_KERNEL_D_MAKE_ROOT
        ZFS_AC_KERNEL_D_OBTAIN_ALIAS
        ZFS_AC_KERNEL_D_PRUNE_ALIASES
        ZFS_AC_KERNEL_D_SET_D_OP
        ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA
        ZFS_AC_KERNEL_CONST_DENTRY_OPERATIONS
        ZFS_AC_KERNEL_S_D_OP
])
