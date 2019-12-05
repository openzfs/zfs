dnl #
dnl # 3.18 API change
dnl # Dentry aliases are in d_u struct dentry member
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_DENTRY_ALIAS_D_U], [
	ZFS_LINUX_TEST_SRC([dentry_alias_d_u], [
		#include <linux/fs.h>
		#include <linux/dcache.h>
		#include <linux/list.h>
	], [
		struct inode *inode __attribute__ ((unused)) = NULL;
		struct dentry *dentry __attribute__ ((unused)) = NULL;
		hlist_for_each_entry(dentry, &inode->i_dentry,
		    d_u.d_alias) {
			d_drop(dentry);
		}
	])
])

AC_DEFUN([ZFS_AC_KERNEL_DENTRY_ALIAS_D_U], [
	AC_MSG_CHECKING([whether dentry aliases are in d_u member])
	ZFS_LINUX_TEST_RESULT([dentry_alias_d_u], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DENTRY_D_U_ALIASES, 1,
		    [dentry aliases are in d_u member])
	],[
		AC_MSG_RESULT(no)
	])
])

