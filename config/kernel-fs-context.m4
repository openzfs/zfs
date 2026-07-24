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
		AC_MSG_ERROR([
	*** This kernel does not have `struct fs_context`. OpenZFS cannot be compiled.
		])
        ])
])

dnl #
dnl # 6.18 API change
dnl # vfs_parse_fs_string() 4th arg removed; length generated internally by
dnl # strlen().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_PARSE_FS_STRING_3ARGS], [
	ZFS_LINUX_TEST_SRC([vfs_parse_fs_string_3args], [
		#include <linux/fs.h>
		#include <linux/fs_context.h>
        ],[
		vfs_parse_fs_string(NULL, NULL, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_PARSE_FS_STRING_3ARGS], [
        AC_MSG_CHECKING([whether vfs_parse_fs_string() takes 3 args])
        ZFS_LINUX_TEST_RESULT([vfs_parse_fs_string_3args], [
                AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_PARSE_FS_STRING_3ARGS, 1,
		    [vfs_parse_fs_string() takes 3 args])
        ],[
		AC_MSG_RESULT(no)
        ])
])
