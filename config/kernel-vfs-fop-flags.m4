dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Linux 6.12 collected a set of per-file capability flags into a fop_flags
dnl # member of struct file_operations.  The FOP_* flags themselves are plain
dnl # defines that the source can test with #ifdef, so only the member needs
dnl # to be detected here.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_FOP_FLAGS], [
	ZFS_LINUX_TEST_SRC([vfs_has_fop_flags], [
		#include <linux/fs.h>

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.fop_flags	= 0,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_FOP_FLAGS], [
	AC_MSG_CHECKING([whether file_operations has fop_flags])
	ZFS_LINUX_TEST_RESULT([vfs_has_fop_flags], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_FOP_FLAGS, 1,
		    [file_operations has fop_flags])
	],[
		AC_MSG_RESULT([no])
	])
])
