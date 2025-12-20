dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 6.19 API change. inode->i_state no longer accessible directly; helper
dnl # functions exist.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_STATE_READ_ONCE], [
	ZFS_LINUX_TEST_SRC([inode_state_read_once], [
		#include <linux/fs.h>
	], [
		struct inode i = {};
		inode_state_read_once(&i);
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_STATE_READ_ONCE], [
	AC_MSG_CHECKING([whether inode_state_read_once() exists])
	ZFS_LINUX_TEST_RESULT([inode_state_read_once], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_STATE_READ_ONCE, 1,
		    [inode_state_read_once() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
