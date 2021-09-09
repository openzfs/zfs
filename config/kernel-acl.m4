dnl #
dnl # Check if posix_acl_release can be used from a ZFS_META_LICENSED
dnl # module.  The is_owner_or_cap macro was replaced by
dnl # inode_owner_or_capable
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_POSIX_ACL_RELEASE], [
	ZFS_LINUX_TEST_SRC([posix_acl_release], [
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	], [
		struct posix_acl *tmp = posix_acl_alloc(1, 0);
		posix_acl_release(tmp);
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_RELEASE], [
	AC_MSG_CHECKING([whether posix_acl_release() is available])
	ZFS_LINUX_TEST_RESULT([posix_acl_release], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_RELEASE, 1,
		    [posix_acl_release() is available])

		AC_MSG_CHECKING([whether posix_acl_release() is GPL-only])
		ZFS_LINUX_TEST_RESULT([posix_acl_release_license], [
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_POSIX_ACL_RELEASE_GPL_ONLY, 1,
			    [posix_acl_release() is GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.14 API change,
dnl # set_cached_acl() and forget_cached_acl() changed from inline to
dnl # EXPORT_SYMBOL. In the former case, they may not be usable because of
dnl # posix_acl_release. In the latter case, we can always use them.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SET_CACHED_ACL_USABLE], [
	ZFS_LINUX_TEST_SRC([set_cached_acl], [
		#include <linux/cred.h>
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	], [
		struct inode *ip = NULL;
		struct posix_acl *acl = posix_acl_alloc(1, 0);
		set_cached_acl(ip, ACL_TYPE_ACCESS, acl);
		forget_cached_acl(ip, ACL_TYPE_ACCESS);
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_SET_CACHED_ACL_USABLE], [
	AC_MSG_CHECKING([whether set_cached_acl() is usable])
	ZFS_LINUX_TEST_RESULT([set_cached_acl_license], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_CACHED_ACL_USABLE, 1,
		    [set_cached_acl() is usable])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # posix_acl_chmod() was added as the preferred interface.
dnl #
dnl # 3.14 API change,
dnl # posix_acl_chmod() was changed to __posix_acl_chmod()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_POSIX_ACL_CHMOD], [
	ZFS_LINUX_TEST_SRC([posix_acl_chmod], [
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		posix_acl_chmod(NULL, 0, 0)
	])

	ZFS_LINUX_TEST_SRC([__posix_acl_chmod], [
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		__posix_acl_chmod(NULL, 0, 0)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_CHMOD], [
	AC_MSG_CHECKING([whether __posix_acl_chmod exists])
	ZFS_LINUX_TEST_RESULT([__posix_acl_chmod], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE___POSIX_ACL_CHMOD, 1,
		    [__posix_acl_chmod() exists])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether posix_acl_chmod exists])
		ZFS_LINUX_TEST_RESULT([posix_acl_chmod], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_POSIX_ACL_CHMOD, 1,
			    [posix_acl_chmod() exists])
		],[
			ZFS_LINUX_TEST_ERROR([posix_acl_chmod()])
		])
	])
])

dnl #
dnl # 3.1 API change,
dnl # posix_acl_equiv_mode now wants an umode_t instead of a mode_t
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T], [
	ZFS_LINUX_TEST_SRC([posix_acl_equiv_mode], [
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		umode_t tmp;
		posix_acl_equiv_mode(NULL, &tmp);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T], [
	AC_MSG_CHECKING([whether posix_acl_equiv_mode() wants umode_t])
	ZFS_LINUX_TEST_RESULT([posix_acl_equiv_mode], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([posix_acl_equiv_mode()])
	])
])

dnl #
dnl # 4.8 API change,
dnl # The function posix_acl_valid now must be passed a namespace.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_POSIX_ACL_VALID_WITH_NS], [
	ZFS_LINUX_TEST_SRC([posix_acl_valid_with_ns], [
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
	],[
		struct user_namespace *user_ns = NULL;
		const struct posix_acl *acl = NULL;
		int error;

		error = posix_acl_valid(user_ns, acl);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_POSIX_ACL_VALID_WITH_NS], [
	AC_MSG_CHECKING([whether posix_acl_valid() wants user namespace])
	ZFS_LINUX_TEST_RESULT([posix_acl_valid_with_ns], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_POSIX_ACL_VALID_WITH_NS, 1,
		    [posix_acl_valid() wants user namespace])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.1 API change,
dnl # Check if inode_operations contains the function get_acl
dnl #
dnl # 5.15 API change,
dnl # Added the bool rcu argument to get_acl for rcu path walk.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_GET_ACL], [
	ZFS_LINUX_TEST_SRC([inode_operations_get_acl], [
		#include <linux/fs.h>

		struct posix_acl *get_acl_fn(struct inode *inode, int type)
		    { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_acl = get_acl_fn,
		};
	],[])

	ZFS_LINUX_TEST_SRC([inode_operations_get_acl_rcu], [
		#include <linux/fs.h>

		struct posix_acl *get_acl_fn(struct inode *inode, int type,
		    bool rcu) { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_acl = get_acl_fn,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL], [
	AC_MSG_CHECKING([whether iops->get_acl() exists])
	ZFS_LINUX_TEST_RESULT([inode_operations_get_acl], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_ACL, 1, [iops->get_acl() exists])
	],[
		ZFS_LINUX_TEST_RESULT([inode_operations_get_acl_rcu], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GET_ACL_RCU, 1, [iops->get_acl() takes rcu])
		],[
			ZFS_LINUX_TEST_ERROR([iops->get_acl()])
		])
	])
])

dnl #
dnl # 3.14 API change,
dnl # Check if inode_operations contains the function set_acl
dnl #
dnl # 5.12 API change,
dnl # set_acl() added a user_namespace* parameter first
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_SET_ACL], [
	ZFS_LINUX_TEST_SRC([inode_operations_set_acl_userns], [
		#include <linux/fs.h>

		int set_acl_fn(struct user_namespace *userns,
		    struct inode *inode, struct posix_acl *acl,
		    int type) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_set_acl], [
		#include <linux/fs.h>

		int set_acl_fn(struct inode *inode, struct posix_acl *acl,
		    int type) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL], [
	AC_MSG_CHECKING([whether iops->set_acl() exists])
	ZFS_LINUX_TEST_RESULT([inode_operations_set_acl_userns], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_ACL, 1, [iops->set_acl() exists])
		AC_DEFINE(HAVE_SET_ACL_USERNS, 1, [iops->set_acl() takes 4 args])
	],[
		ZFS_LINUX_TEST_RESULT([inode_operations_set_acl], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SET_ACL, 1, [iops->set_acl() exists, takes 3 args])
		],[
			AC_MSG_RESULT(no)
		])
	])
])

dnl #
dnl # 4.7 API change,
dnl # The kernel get_acl will now check cache before calling i_op->get_acl and
dnl # do set_cached_acl after that, so i_op->get_acl don't need to do that
dnl # anymore.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GET_ACL_HANDLE_CACHE], [
	ZFS_LINUX_TEST_SRC([get_acl_handle_cache], [
		#include <linux/fs.h>
	],[
		void *sentinel __attribute__ ((unused)) =
		    uncached_acl_sentinel(NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GET_ACL_HANDLE_CACHE], [
	AC_MSG_CHECKING([whether uncached_acl_sentinel() exists])
	ZFS_LINUX_TEST_RESULT([get_acl_handle_cache], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_GET_ACL_HANDLE_CACHE, 1,
		    [uncached_acl_sentinel() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.16 kernel: check if struct posix_acl acl.a_refcount is a refcount_t.
dnl # It's an atomic_t on older kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_ACL_HAS_REFCOUNT], [
	ZFS_LINUX_TEST_SRC([acl_refcount], [
		#include <linux/backing-dev.h>
		#include <linux/refcount.h>
		#include <linux/posix_acl.h>
	],[
		struct posix_acl acl;
		refcount_t *r __attribute__ ((unused)) = &acl.a_refcount;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_ACL_HAS_REFCOUNT], [
	AC_MSG_CHECKING([whether posix_acl has refcount_t])
	ZFS_LINUX_TEST_RESULT([acl_refcount], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ACL_REFCOUNT, 1, [posix_acl has refcount_t])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_ACL], [
	ZFS_AC_KERNEL_SRC_POSIX_ACL_RELEASE
	ZFS_AC_KERNEL_SRC_SET_CACHED_ACL_USABLE
	ZFS_AC_KERNEL_SRC_POSIX_ACL_CHMOD
	ZFS_AC_KERNEL_SRC_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T
	ZFS_AC_KERNEL_SRC_POSIX_ACL_VALID_WITH_NS
	ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_GET_ACL
	ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_SET_ACL
	ZFS_AC_KERNEL_SRC_GET_ACL_HANDLE_CACHE
	ZFS_AC_KERNEL_SRC_ACL_HAS_REFCOUNT
])

AC_DEFUN([ZFS_AC_KERNEL_ACL], [
	ZFS_AC_KERNEL_POSIX_ACL_RELEASE
	ZFS_AC_KERNEL_SET_CACHED_ACL_USABLE
	ZFS_AC_KERNEL_POSIX_ACL_CHMOD
	ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T
	ZFS_AC_KERNEL_POSIX_ACL_VALID_WITH_NS
	ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL
	ZFS_AC_KERNEL_GET_ACL_HANDLE_CACHE
	ZFS_AC_KERNEL_ACL_HAS_REFCOUNT
])
