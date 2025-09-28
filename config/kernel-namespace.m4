dnl #
dnl # 6.18 API change
dnl # ns->ops->type was moved to ns->ns.ns_type (struct ns_common)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_NS_COMMON_TYPE], [
	ZFS_LINUX_TEST_SRC([ns_common_type], [
		#include <linux/user_namespace.h>
	],[
		struct user_namespace ns;
		ns.ns.ns_type = 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_NS_COMMON_TYPE], [
	AC_MSG_CHECKING([whether ns_type is accessible through ns_common])
	ZFS_LINUX_TEST_RESULT([ns_common_type], [
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_NS_COMMON_TYPE], 1,
			[Define if ns_type is accessible through ns_common])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_NAMESPACE], [
	ZFS_AC_KERNEL_SRC_NS_COMMON_TYPE
])

AC_DEFUN([ZFS_AC_KERNEL_NAMESPACE], [
	ZFS_AC_KERNEL_NS_COMMON_TYPE
])
