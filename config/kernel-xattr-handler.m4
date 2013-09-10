dnl #
dnl # 2.6.35 API change,
dnl # The 'struct xattr_handler' was constified in the generic
dnl # super_block structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONST_XATTR_HANDLER],
	[AC_MSG_CHECKING([whether super_block uses const struct xattr_hander])
	ZFS_LINUX_TRY_COMPILE([
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
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_CONST_XATTR_HANDLER, 1,
		          [super_block uses const struct xattr_hander])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # 2.6.33 API change,
dnl # The xattr_hander->get() callback was changed to take a dentry
dnl # instead of an inode, and a handler_flags argument was added.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_GET], [
	AC_MSG_CHECKING([whether xattr_handler->get() wants dentry])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/xattr.h>

		int get(struct dentry *dentry, const char *name,
		    void *buffer, size_t size, int handler_flags) { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.get = get,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DENTRY_XATTR_GET, 1,
		    [xattr_handler->get() wants dentry])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.33 API change,
dnl # The xattr_hander->set() callback was changed to take a dentry
dnl # instead of an inode, and a handler_flags argument was added.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_SET], [
	AC_MSG_CHECKING([whether xattr_handler->set() wants dentry])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/xattr.h>

		int set(struct dentry *dentry, const char *name,
		    const void *buffer, size_t size, int flags,
		    int handler_flags) { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.set = set,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DENTRY_XATTR_SET, 1,
		    [xattr_handler->set() wants dentry])
	],[
		AC_MSG_RESULT(no)
	])
])
