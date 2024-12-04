dnl #
dnl # 2.6.38 API change
dnl # ns_capable() was introduced
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_NS_CAPABLE], [
	ZFS_LINUX_TEST_SRC([ns_capable], [
		#include <linux/capability.h>
	],[
		ns_capable((struct user_namespace *)NULL, CAP_SYS_ADMIN);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_NS_CAPABLE], [
	AC_MSG_CHECKING([whether ns_capable exists])
	ZFS_LINUX_TEST_RESULT([ns_capable], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([ns_capable()])
	])
])

dnl #
dnl # 2.6.39 API change
dnl # struct user_namespace was added to struct cred_t as cred->user_ns member
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CRED_USER_NS], [
	ZFS_LINUX_TEST_SRC([cred_user_ns], [
		#include <linux/cred.h>
	],[
		struct cred cr;
		cr.user_ns = (struct user_namespace *)NULL;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CRED_USER_NS], [
	AC_MSG_CHECKING([whether cred_t->user_ns exists])
	ZFS_LINUX_TEST_RESULT([cred_user_ns], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([cred_t->user_ns()])
	])
])

dnl #
dnl # 3.4 API change
dnl # kuid_has_mapping() and kgid_has_mapping() were added to distinguish
dnl # between internal kernel uids/gids and user namespace uids/gids.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KUID_HAS_MAPPING], [
	ZFS_LINUX_TEST_SRC([kuid_has_mapping], [
		#include <linux/uidgid.h>
	],[
		kuid_has_mapping((struct user_namespace *)NULL, KUIDT_INIT(0));
		kgid_has_mapping((struct user_namespace *)NULL, KGIDT_INIT(0));
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KUID_HAS_MAPPING], [
	AC_MSG_CHECKING([whether kuid_has_mapping/kgid_has_mapping exist])
	ZFS_LINUX_TEST_RESULT([kuid_has_mapping], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([kuid_has_mapping()])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_USERNS_CAPABILITIES], [
	ZFS_AC_KERNEL_SRC_NS_CAPABLE
	ZFS_AC_KERNEL_SRC_HAS_CAPABILITY
	ZFS_AC_KERNEL_SRC_CRED_USER_NS
	ZFS_AC_KERNEL_SRC_KUID_HAS_MAPPING
])

AC_DEFUN([ZFS_AC_KERNEL_USERNS_CAPABILITIES], [
	ZFS_AC_KERNEL_NS_CAPABLE
	ZFS_AC_KERNEL_HAS_CAPABILITY
	ZFS_AC_KERNEL_CRED_USER_NS
	ZFS_AC_KERNEL_KUID_HAS_MAPPING
])
