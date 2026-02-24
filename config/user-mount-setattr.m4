dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Check for mount_setattr() and struct mount_attr availability
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_MOUNT_SETATTR], [
	AC_CHECK_FUNC([mount_setattr], [
		AC_MSG_CHECKING([for struct mount_attr])
		AC_COMPILE_IFELSE([
			AC_LANG_PROGRAM([[
				#include <sys/mount.h>
			]], [[
				struct mount_attr attr = {
				    .attr_set = MOUNT_ATTR_RDONLY,
				    .attr_clr = MOUNT_ATTR_NOEXEC,
				};
				(void) attr;
			]])
		], [
			AC_MSG_RESULT([yes])
			AC_DEFINE([HAVE_MOUNT_SETATTR], [1],
			    [mount_setattr() and struct mount_attr
			    are available])
		], [
			AC_MSG_RESULT([no])
		])
	])
])
