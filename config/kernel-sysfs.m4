dnl #
dnl # Linux 5.2/5.18 API
dnl #
dnl # In cdb4f26a63c391317e335e6e683a614358e70aeb ("kobject: kobj_type: remove default_attrs")
dnl # 	struct kobj_type.default_attrs
dnl # was finally removed in favour of
dnl # 	struct kobj_type.default_groups
dnl #
dnl # This was added in aa30f47cf666111f6bbfd15f290a27e8a7b9d854 ("kobject: Add support for default attribute groups to kobj_type"),
dnl # if both are present (5.2-5.17), we prefer default_groups; they're otherwise equivalent
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SYSFS_DEFAULT_GROUPS], [
	ZFS_LINUX_TEST_SRC([sysfs_default_groups], [
		#include <linux/kobject.h>
	],[
		struct kobj_type __attribute__ ((unused)) kt = {
			.default_groups = (const struct attribute_group **)NULL };
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SYSFS_DEFAULT_GROUPS], [
	AC_MSG_CHECKING([whether struct kobj_type.default_groups exists])
	ZFS_LINUX_TEST_RESULT([sysfs_default_groups],[
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_SYSFS_DEFAULT_GROUPS], 1, [struct kobj_type has default_groups])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SYSFS], [
	ZFS_AC_KERNEL_SRC_SYSFS_DEFAULT_GROUPS
])

AC_DEFUN([ZFS_AC_KERNEL_SYSFS], [
	ZFS_AC_KERNEL_SYSFS_DEFAULT_GROUPS
])
