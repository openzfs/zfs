dnl #
dnl # 2.6.35 API change,
dnl # The 'struct xattr_handler' was constified in the generic
dnl # super_block structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONST_XATTR_HANDLER], [
	ZFS_LINUX_TEST_SRC([const_xattr_handler], [
		#include <linux/fs.h>
		#include <linux/xattr.h>

		const struct xattr_handler xattr_test_handler = {
			.prefix	= "test",
			.get	= NULL,
			.set	= NULL,
		};

		const struct xattr_handler *xattr_handlers[] = {
			&xattr_test_handler,
		};

		const struct super_block sb __attribute__ ((unused)) = {
			.s_xattr = xattr_handlers,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_CONST_XATTR_HANDLER], [
	AC_MSG_CHECKING([whether super_block uses const struct xattr_handler])
	ZFS_LINUX_TEST_RESULT([const_xattr_handler], [
		AC_MSG_RESULT([yes])
	],[
		ZFS_LINUX_TEST_ERROR([const xattr_handler])
	])
])

dnl #
dnl # Android API change,
dnl # The xattr_handler->get() callback was
dnl # changed to take dentry, inode and flags.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_XATTR_HANDLER_GET_DENTRY_INODE_FLAGS], [
	ZFS_LINUX_TEST_SRC([xattr_handler_get_dentry_inode_flags], [
		#include <linux/xattr.h>

		static int get(const struct xattr_handler *handler,
		    struct dentry *dentry, struct inode *inode,
		    const char *name, void *buffer,
		    size_t size, int flags) { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.get = get,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_GET_DENTRY_INODE_FLAGS], [
	AC_MSG_RESULT(no)
	AC_MSG_CHECKING(
	    [whether xattr_handler->get() wants dentry and inode and flags])
	ZFS_LINUX_TEST_RESULT([xattr_handler_get_dentry_inode_flags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_XATTR_GET_DENTRY_INODE_FLAGS, 1,
		    [xattr_handler->get() wants dentry and inode and flags])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Supported xattr handler set() interfaces checked newest to oldest.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_XATTR_HANDLER_SET], [
	ZFS_LINUX_TEST_SRC([xattr_handler_set_mnt_idmap], [
		#include <linux/xattr.h>

		static int set(const struct xattr_handler *handler,
			struct mnt_idmap *idmap,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *buffer,
			size_t size, int flags)
			{ return 0; }
		static const struct xattr_handler
			xops __attribute__ ((unused)) = {
			.set = set,
		};
	],[])

	ZFS_LINUX_TEST_SRC([xattr_handler_set_userns], [
		#include <linux/xattr.h>

		static int set(const struct xattr_handler *handler,
			struct user_namespace *mnt_userns,
			struct dentry *dentry, struct inode *inode,
			const char *name, const void *buffer,
			size_t size, int flags)
			{ return 0; }
		static const struct xattr_handler
			xops __attribute__ ((unused)) = {
			.set = set,
		};
	],[])

	ZFS_LINUX_TEST_SRC([xattr_handler_set_dentry_inode], [
		#include <linux/xattr.h>

		static int set(const struct xattr_handler *handler,
		    struct dentry *dentry, struct inode *inode,
		    const char *name, const void *buffer,
		    size_t size, int flags)
		    { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.set = set,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_SET], [
	dnl #
	dnl # 5.12 API change,
	dnl # The xattr_handler->set() callback was changed to 8 arguments, and
	dnl # struct user_namespace* was inserted as arg #2
	dnl #
	dnl # 6.3 API change,
	dnl # The xattr_handler->set() callback 2nd arg is now struct mnt_idmap *
	dnl #
	AC_MSG_CHECKING([whether xattr_handler->set() wants dentry, inode, and mnt_idmap])
	ZFS_LINUX_TEST_RESULT([xattr_handler_set_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_XATTR_SET_IDMAP, 1,
		    [xattr_handler->set() takes mnt_idmap])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether xattr_handler->set() wants dentry, inode, and user_namespace])
		ZFS_LINUX_TEST_RESULT([xattr_handler_set_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_XATTR_SET_USERNS, 1,
			    [xattr_handler->set() takes user_namespace])
		],[
			dnl #
			dnl # 4.7 API change,
			dnl # The xattr_handler->set() callback was changed to take both
			dnl # dentry and inode.
			dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether xattr_handler->set() wants dentry and inode])
			ZFS_LINUX_TEST_RESULT([xattr_handler_set_dentry_inode], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_XATTR_SET_DENTRY_INODE, 1,
				    [xattr_handler->set() wants both dentry and inode])
			],[
				ZFS_LINUX_TEST_ERROR([xattr set()])
			])
		])
	])
])

dnl #
dnl # 4.9 API change,
dnl # iops->{set,get,remove}xattr and generic_{set,get,remove}xattr are
dnl # removed. xattr operations will directly go through sb->s_xattr.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_SETXATTR], [
	ZFS_LINUX_TEST_SRC([have_generic_setxattr], [
		#include <linux/fs.h>
		#include <linux/xattr.h>

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.setxattr = generic_setxattr
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_XATTR], [
	ZFS_AC_KERNEL_SRC_CONST_XATTR_HANDLER
	ZFS_AC_KERNEL_SRC_XATTR_HANDLER_GET_DENTRY_INODE_FLAGS
	ZFS_AC_KERNEL_SRC_XATTR_HANDLER_SET
])

AC_DEFUN([ZFS_AC_KERNEL_XATTR], [
	ZFS_AC_KERNEL_CONST_XATTR_HANDLER
	ZFS_AC_KERNEL_XATTR_HANDLER_GET_DENTRY_INODE_FLAGS
	ZFS_AC_KERNEL_XATTR_HANDLER_SET
])
