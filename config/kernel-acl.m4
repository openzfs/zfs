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
dnl # 6.2 API change,
dnl # get_acl() was renamed to get_inode_acl()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_GET_ACL], [
	ZFS_LINUX_TEST_SRC([inode_operations_get_acl], [
		#include <linux/fs.h>

		static struct posix_acl *get_acl_fn(struct inode *inode, int type)
		    { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_acl = get_acl_fn,
		};
	],[])

	ZFS_LINUX_TEST_SRC([inode_operations_get_acl_rcu], [
		#include <linux/fs.h>

		static struct posix_acl *get_acl_fn(struct inode *inode, int type,
		    bool rcu) { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_acl = get_acl_fn,
		};
	],[])

	ZFS_LINUX_TEST_SRC([inode_operations_get_inode_acl], [
		#include <linux/fs.h>

		static struct posix_acl *get_inode_acl_fn(struct inode *inode, int type,
		    bool rcu) { return NULL; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.get_inode_acl = get_inode_acl_fn,
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
			ZFS_LINUX_TEST_RESULT([inode_operations_get_inode_acl], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_GET_INODE_ACL, 1, [has iops->get_inode_acl()])
			],[
				ZFS_LINUX_TEST_ERROR([iops->get_acl() or iops->get_inode_acl()])
			])
		])
	])
])

dnl #
dnl # 5.12 API change,
dnl # set_acl() added a user_namespace* parameter first
dnl #
dnl # 6.2 API change,
dnl # set_acl() second paramter changed to a struct dentry *
dnl #
dnl # 6.3 API change,
dnl # set_acl() first parameter changed to struct mnt_idmap *
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_SET_ACL], [
	ZFS_LINUX_TEST_SRC([inode_operations_set_acl_mnt_idmap_dentry], [
		#include <linux/fs.h>

		static int set_acl_fn(struct mnt_idmap *idmap,
		    struct dentry *dent, struct posix_acl *acl,
		    int type) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_set_acl_userns_dentry], [
		#include <linux/fs.h>

		static int set_acl_fn(struct user_namespace *userns,
		    struct dentry *dent, struct posix_acl *acl,
		    int type) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_set_acl_userns], [
		#include <linux/fs.h>

		static int set_acl_fn(struct user_namespace *userns,
		    struct inode *inode, struct posix_acl *acl,
		    int type) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_set_acl], [
		#include <linux/fs.h>

		static int set_acl_fn(struct inode *inode, struct posix_acl *acl,
		    int type) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.set_acl = set_acl_fn,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL], [
	AC_MSG_CHECKING([whether iops->set_acl() with 4 args exists])
	ZFS_LINUX_TEST_RESULT([inode_operations_set_acl_userns], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_ACL_USERNS, 1, [iops->set_acl() takes 4 args])
	],[
		ZFS_LINUX_TEST_RESULT([inode_operations_set_acl_mnt_idmap_dentry], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SET_ACL_IDMAP_DENTRY, 1,
			    [iops->set_acl() takes 4 args, arg1 is struct mnt_idmap *])
		],[
			ZFS_LINUX_TEST_RESULT([inode_operations_set_acl_userns_dentry], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_SET_ACL_USERNS_DENTRY_ARG2, 1,
				    [iops->set_acl() takes 4 args, arg2 is struct dentry *])
			],[
				AC_MSG_RESULT(no)
			])
		])
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
	ZFS_AC_KERNEL_SRC_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T
	ZFS_AC_KERNEL_SRC_POSIX_ACL_VALID_WITH_NS
	ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_GET_ACL
	ZFS_AC_KERNEL_SRC_INODE_OPERATIONS_SET_ACL
	ZFS_AC_KERNEL_SRC_ACL_HAS_REFCOUNT
])

AC_DEFUN([ZFS_AC_KERNEL_ACL], [
	ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T
	ZFS_AC_KERNEL_POSIX_ACL_VALID_WITH_NS
	ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL
	ZFS_AC_KERNEL_ACL_HAS_REFCOUNT
])
