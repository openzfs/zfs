dnl #
dnl # 2.6.35 API change,
dnl # The 'struct xattr_handler' was constified in the generic
dnl # super_block structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONST_XATTR_HANDLER], [
	AC_MSG_CHECKING([whether super_block uses const struct xattr_hander])
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
dnl # Supported xattr handler get() interfaces checked newest to oldest.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_GET], [
	dnl #
	dnl # 4.4 API change,
	dnl # The xattr_hander->get() callback was changed to take a
	dnl # attr_handler, and handler_flags argument was removed and
	dnl # should be accessed by handler->flags.
	dnl #
	AC_MSG_CHECKING([whether xattr_handler->get() wants xattr_handler])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/xattr.h>

		int get(const struct xattr_handler *handler,
		    struct dentry *dentry, const char *name,
		    void *buffer, size_t size) { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.get = get,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_XATTR_GET_HANDLER, 1,
		    [xattr_handler->get() wants xattr_handler])
	],[
		dnl #
		dnl # 2.6.33 API change,
		dnl # The xattr_hander->get() callback was changed to take
		dnl # a dentry instead of an inode, and a handler_flags
		dnl # argument was added.
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether xattr_handler->get() wants dentry])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/xattr.h>

			int get(struct dentry *dentry, const char *name,
			    void *buffer, size_t size, int handler_flags)
			    { return 0; }
			static const struct xattr_handler
			    xops __attribute__ ((unused)) = {
				.get = get,
			};
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_XATTR_GET_DENTRY, 1,
			    [xattr_handler->get() wants dentry])
		],[
			dnl #
			dnl # 2.6.32 API
			dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING(
			    [whether xattr_handler->get() wants inode])
			ZFS_LINUX_TRY_COMPILE([
				#include <linux/xattr.h>

				int get(struct inode *ip, const char *name,
				    void *buffer, size_t size) { return 0; }
				static const struct xattr_handler
				    xops __attribute__ ((unused)) = {
					.get = get,
				};
			],[
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_XATTR_GET_INODE, 1,
				    [xattr_handler->get() wants inode])
			],[
	                        AC_MSG_ERROR([no; please file a bug report])
			])
		])
	])
])

dnl #
dnl # Supported xattr handler set() interfaces checked newest to oldest.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_SET], [
	dnl #
	dnl # 4.4 API change,
	dnl # The xattr_hander->set() callback was changed to take a
	dnl # xattr_handler, and handler_flags argument was removed and
	dnl # should be accessed by handler->flags.
	dnl #
	AC_MSG_CHECKING([whether xattr_handler->set() wants xattr_handler])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/xattr.h>

		int set(const struct xattr_handler *handler,
		    struct dentry *dentry, const char *name,
		    const void *buffer, size_t size, int flags)
		    { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.set = set,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_XATTR_SET_HANDLER, 1,
		    [xattr_handler->set() wants xattr_handler])
	],[
		dnl #
		dnl # 2.6.33 API change,
		dnl # The xattr_hander->set() callback was changed to take a
		dnl # dentry instead of an inode, and a handler_flags
		dnl # argument was added.
		dnl #
		AC_MSG_RESULT(no)
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
			AC_DEFINE(HAVE_XATTR_SET_DENTRY, 1,
			    [xattr_handler->set() wants dentry])
		],[
			dnl #
			dnl # 2.6.32 API
			dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING(
			    [whether xattr_handler->set() wants inode])
			ZFS_LINUX_TRY_COMPILE([
				#include <linux/xattr.h>

				int set(struct inode *ip, const char *name,
				    const void *buffer, size_t size, int flags)
				    { return 0; }
				static const struct xattr_handler
				    xops __attribute__ ((unused)) = {
					.set = set,
				};
			],[
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_XATTR_SET_INODE, 1,
				    [xattr_handler->set() wants inode])
			],[
	                        AC_MSG_ERROR([no; please file a bug report])
			])
		])
	])
])

dnl #
dnl # Supported xattr handler list() interfaces checked newest to oldest.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_XATTR_HANDLER_LIST], [
	dnl # 4.5 API change,
	dnl # The xattr_hander->list() callback was changed to take only a
	dnl # dentry and it only needs to return if it's accessable.
	AC_MSG_CHECKING([whether xattr_handler->list() wants simple])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/xattr.h>

		bool list(struct dentry *dentry) { return 0; }
		static const struct xattr_handler
		    xops __attribute__ ((unused)) = {
			.list = list,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_XATTR_LIST_SIMPLE, 1,
		    [xattr_handler->list() wants simple])
	],[
		dnl #
		dnl # 4.4 API change,
		dnl # The xattr_hander->list() callback was changed to take a
		dnl # xattr_handler, and handler_flags argument was removed
		dnl # and should be accessed by handler->flags.
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING(
		    [whether xattr_handler->list() wants xattr_handler])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/xattr.h>

			size_t list(const struct xattr_handler *handler,
			    struct dentry *dentry, char *list, size_t list_size,
			    const char *name, size_t name_len) { return 0; }
			static const struct xattr_handler
			    xops __attribute__ ((unused)) = {
				.list = list,
			};
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_XATTR_LIST_HANDLER, 1,
			    [xattr_handler->list() wants xattr_handler])
		],[
			dnl #
			dnl # 2.6.33 API change,
			dnl # The xattr_hander->list() callback was changed
			dnl # to take a dentry instead of an inode, and a
			dnl # handler_flags argument was added.
			dnl #
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING(
			    [whether xattr_handler->list() wants dentry])
			ZFS_LINUX_TRY_COMPILE([
				#include <linux/xattr.h>

				size_t list(struct dentry *dentry,
				    char *list, size_t list_size,
				    const char *name, size_t name_len,
				    int handler_flags) { return 0; }
				static const struct xattr_handler
				    xops __attribute__ ((unused)) = {
					.list = list,
				};
			],[
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_XATTR_LIST_DENTRY, 1,
				    [xattr_handler->list() wants dentry])
			],[
				dnl #
				dnl # 2.6.32 API
				dnl #
				AC_MSG_RESULT(no)
				AC_MSG_CHECKING(
				    [whether xattr_handler->list() wants inode])
				ZFS_LINUX_TRY_COMPILE([
					#include <linux/xattr.h>

					size_t list(struct inode *ip, char *lst,
					    size_t list_size, const char *name,
					    size_t name_len) { return 0; }
					static const struct xattr_handler
					    xops __attribute__ ((unused)) = {
						.list = list,
					};
				],[
				],[
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_XATTR_LIST_INODE, 1,
					    [xattr_handler->list() wants inode])
				],[
		                        AC_MSG_ERROR(
					    [no; please file a bug report])
				])
			])
		])
	])
])

dnl #
dnl # 3.7 API change,
dnl # The posix_acl_{from,to}_xattr functions gained a new
dnl # parameter: user_ns
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_FROM_XATTR_USERNS], [
	AC_MSG_CHECKING([whether posix_acl_from_xattr() needs user_ns])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl_xattr.h>
	],[
		posix_acl_from_xattr(&init_user_ns, NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_FROM_XATTR_USERNS, 1,
		    [posix_acl_from_xattr() needs user_ns])
	],[
		AC_MSG_RESULT(no)
	])
])

