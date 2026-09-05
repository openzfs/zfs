dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 6.9 API change
dnl # super_set_uuid() was added to set sb->s_uuid and sb->s_uuid_len.
dnl # The VFS serves the FS_IOC_GETFSUUID ioctl on all filesystems that
dnl # have a non-zero sb->s_uuid_len, before it calls the ioctl handler
dnl # of the filesystem.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SUPER_SET_UUID], [
	ZFS_LINUX_TEST_SRC([super_set_uuid], [
		#include <linux/fs.h>
	], [
		static struct super_block sb;
		static const u8 uuid[16];

		super_set_uuid(&sb, uuid, sizeof (uuid));
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SUPER_SET_UUID], [
	AC_MSG_CHECKING([whether super_set_uuid() is available])
	ZFS_LINUX_TEST_RESULT([super_set_uuid], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SUPER_SET_UUID, 1,
		    [Define if super_set_uuid() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
