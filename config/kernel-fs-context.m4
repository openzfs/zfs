dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 2.6.38 API change
dnl # The .get_sb callback has been replaced by a .mount callback
dnl # in the file_system_type structure.
dnl #
dnl # 5.2 API change
dnl # The new fs_context-based filesystem API is introduced, with the old
dnl # one (via file_system_type.mount) preserved as a compatibility shim.
dnl #
dnl # 7.0 API change
dnl # Compatibility shim removed, so all callers must go through the mount API.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FS_CONTEXT], [
	ZFS_LINUX_TEST_SRC([fs_context], [
		#include <linux/fs.h>
		#include <linux/fs_context.h>
        ],[
		static struct fs_context fs __attribute__ ((unused)) = { 0 };
		static struct fs_context *fsp __attribute__ ((unused));
		fsp = vfs_dup_fs_context(&fs);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FS_CONTEXT], [
        AC_MSG_CHECKING([whether fs_context exists])
        ZFS_LINUX_TEST_RESULT([fs_context], [
                AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_CONTEXT, 1, [fs_context exists])
        ],[
		AC_MSG_RESULT(no)
        ])
])
