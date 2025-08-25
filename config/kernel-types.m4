dnl #
dnl # check if kernel provides definitions for given types
dnl #

dnl _ZFS_AC_KERNEL_SRC_TYPE(type)
AC_DEFUN([_ZFS_AC_KERNEL_SRC_TYPE], [
	ZFS_LINUX_TEST_SRC([type_$1], [
		#include <linux/types.h>
	],[
		const $1 __attribute__((unused)) x = ($1) 0;
	])
])

dnl _ZFS_AC_KERNEL_TYPE(type)
AC_DEFUN([_ZFS_AC_KERNEL_TYPE], [
	AC_MSG_CHECKING([whether kernel defines $1])
	ZFS_LINUX_TEST_RESULT([type_$1], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_KERNEL_]m4_quote(m4_translit([$1], [a-z], [A-Z])),
		    1, [kernel defines $1])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl ZFS_AC_KERNEL_TYPES([types...])
AC_DEFUN([ZFS_AC_KERNEL_TYPES], [
	AC_DEFUN([ZFS_AC_KERNEL_SRC_TYPES], [
		m4_foreach_w([type], [$1], [
			_ZFS_AC_KERNEL_SRC_TYPE(type)
		])
	])
	AC_DEFUN([ZFS_AC_KERNEL_TYPES], [
		m4_foreach_w([type], [$1], [
			_ZFS_AC_KERNEL_TYPE(type)
		])
	])
])

ZFS_AC_KERNEL_TYPES([intptr_t])
