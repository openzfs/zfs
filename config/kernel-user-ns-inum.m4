dnl #
dnl # 3.18 API change
dnl # struct user_namespace inum moved from .proc_inum to .ns.inum.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_USER_NS_COMMON_INUM], [
	ZFS_LINUX_TEST_SRC([user_ns_common_inum], [
		#include <linux/user_namespace.h>
	], [
		struct user_namespace uns;
		uns.ns.inum = 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_USER_NS_COMMON_INUM], [
	AC_MSG_CHECKING([whether user_namespace->ns.inum exists])
	ZFS_LINUX_TEST_RESULT([user_ns_common_inum], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_USER_NS_COMMON_INUM, 1,
		    [user_namespace->ns.inum exists])
	],[
		AC_MSG_RESULT(no)
	])
])
