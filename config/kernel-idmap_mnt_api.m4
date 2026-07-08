dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 6.8 decouples mnt_idmap from user_namespace. This is all internal
dnl # to mnt_idmap so we can't detect it directly, but we detect a related
dnl # change as use that as a signal.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_IDMAP_NO_USERNS], [
	ZFS_LINUX_TEST_SRC([idmap_no_userns], [
		#include <linux/uidgid.h>
	], [
		struct uid_gid_map *map = NULL;
		map_id_down(map, 0);
	])
])


AC_DEFUN([ZFS_AC_KERNEL_IDMAP_NO_USERNS], [
	AC_MSG_CHECKING([whether idmapped mounts have a user namespace])
	ZFS_LINUX_TEST_RESULT([idmap_no_userns], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_IDMAP_NO_USERNS, 1,
			[mnt_idmap does not have user_namespace])
	], [
		AC_MSG_RESULT([no])
	])
])
