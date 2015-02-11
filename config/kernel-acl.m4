dnl #
dnl # Check if posix_acl_release can be used from a ZFS_META_LICENSED
dnl # module.  The is_owner_or_cap macro was replaced by
dnl # inode_owner_or_capable
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_RELEASE], [
	AC_MSG_CHECKING([whether posix_acl_release() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		struct posix_acl* tmp = posix_acl_alloc(1, 0);
		posix_acl_release(tmp);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_RELEASE, 1,
		    [posix_acl_release() is available])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether posix_acl_release() is GPL-only])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl.h>

		MODULE_LICENSE("$ZFS_META_LICENSE");
	],[
		struct posix_acl* tmp = posix_acl_alloc(1, 0);
		posix_acl_release(tmp);
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_RELEASE_GPL_ONLY, 1,
		    [posix_acl_release() is GPL-only])
	])
])

dnl #
dnl # 3.1 API change,
dnl # posix_acl_chmod_masq() is not exported anymore and posix_acl_chmod()
dnl # was introduced to replace it.
dnl #
dnl # 3.14 API change,
dnl # posix_acl_chmod() is changed to __posix_acl_chmod()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_CHMOD], [
	AC_MSG_CHECKING([whether posix_acl_chmod exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		posix_acl_chmod(NULL, 0, 0)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_CHMOD, 1, [posix_acl_chmod() exists])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether __posix_acl_chmod exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		__posix_acl_chmod(NULL, 0, 0)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE___POSIX_ACL_CHMOD, 1, [__posix_acl_chmod() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.30 API change,
dnl # caching of ACL into the inode was added in this version.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_CACHING], [
	AC_MSG_CHECKING([whether inode has i_acl and i_default_acl])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode ino;
		ino.i_acl = NULL;
		ino.i_default_acl = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_CACHING, 1,
		    [inode contains i_acl and i_default_acl])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # posix_acl_equiv_mode now wants an umode_t* instead of a mode_t*
dnl #
AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T], [
	AC_MSG_CHECKING([whether posix_acl_equiv_mode() wants umode_t])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		umode_t tmp;
		posix_acl_equiv_mode(NULL,&tmp);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_EQUIV_MODE_UMODE_T, 1,
		    [ posix_acl_equiv_mode wants umode_t*])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.27 API change,
dnl # Check if inode_operations contains the function permission
dnl # and expects the nameidata structure to have been removed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION], [
	AC_MSG_CHECKING([whether iops->permission() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int permission_fn(struct inode *inode, int mask) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.permission = permission_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PERMISSION, 1, [iops->permission() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.26 API change,
dnl # Check if inode_operations contains the function permission
dnl # and expects the nameidata structure to be passed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION_WITH_NAMEIDATA], [
	AC_MSG_CHECKING([whether iops->permission() wants nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int permission_fn(struct inode *inode, int mask,
		    struct nameidata *nd) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.permission = permission_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PERMISSION, 1, [iops->permission() exists])
		AC_DEFINE(HAVE_PERMISSION_WITH_NAMEIDATA, 1,
		    [iops->permission() with nameidata exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.32 API change,
dnl # Check if inode_operations contains the function check_acl
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL], [
	AC_MSG_CHECKING([whether iops->check_acl() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int check_acl_fn(struct inode *inode, int mask) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.check_acl = check_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CHECK_ACL, 1, [iops->check_acl() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.38 API change,
dnl # The function check_acl gained a new parameter: flags
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL_WITH_FLAGS], [
	AC_MSG_CHECKING([whether iops->check_acl() wants flags])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int check_acl_fn(struct inode *inode, int mask,
		    unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.check_acl = check_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CHECK_ACL, 1, [iops->check_acl() exists])
		AC_DEFINE(HAVE_CHECK_ACL_WITH_FLAGS, 1,
		    [iops->check_acl() wants flags])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # Check if inode_operations contains the function get_acl
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL], [
	AC_MSG_CHECKING([whether iops->get_acl() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		struct posix_acl *get_acl_fn(struct inode *inode, int type)
		    { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_acl = get_acl_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_ACL, 1, [iops->get_acl() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.30 API change,
dnl # current_umask exists only since this version.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CURRENT_UMASK], [
	AC_MSG_CHECKING([whether current_umask exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		current_umask();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CURRENT_UMASK, 1, [current_umask() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
