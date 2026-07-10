dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 2.6.38 API change
dnl # follow_down() renamed follow_down_one().  The original follow_down()
dnl # symbol still exists but will traverse down all the layers.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FOLLOW_DOWN_ONE], [
	ZFS_LINUX_TEST_SRC([follow_down_one], [
		#include <linux/namei.h>
	],[
		struct path *p = NULL;
		follow_down_one(p);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FOLLOW_DOWN_ONE], [
	AC_MSG_CHECKING([whether follow_down_one() is available])
	ZFS_LINUX_TEST_RESULT([follow_down_one], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([follow_down_one()])
	])
])

dnl #
dnl # 6.3: look_down() gains a second flags param. Before this, we need
dnl #      vfs_path_lookup() to build an emulation.
dnl #
dnl # 3.12: vfs_path_lookup unpublished from linux/namei.h, remains exported
dnl # 6.4: vfs_path_lookup republished in linux/namei.h
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FOLLOW_DOWN_FLAGS], [
	ZFS_LINUX_TEST_SRC([follow_down_flags], [
		#include <linux/namei.h>
	],[
		struct path *p = NULL;
		follow_down(p, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FOLLOW_DOWN_FLAGS], [
	AC_MSG_CHECKING([whether follow_down() takes a flags parameter])
	ZFS_LINUX_TEST_RESULT([follow_down_flags], [
		AC_DEFINE(HAVE_FOLLOW_DOWN_FLAGS, 1,
			[follow_down() takes a flags parameter])
		AC_MSG_RESULT(yes)
	],[
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether vfs_path_lookup() is exported])
		ZFS_CHECK_SYMBOL_EXPORT(
		    [vfs_path_lookup], [fs/namei.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_VFS_PATH_LOOKUP_EXPORTED, 1,
			    [vfs_path_lookup() is exported])
		],[
			AC_MSG_RESULT(no)
			ZFS_LINUX_TEST_ERROR([follow_down()])
		])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_FOLLOW_DOWN], [
	ZFS_AC_KERNEL_SRC_FOLLOW_DOWN_ONE
	ZFS_AC_KERNEL_SRC_FOLLOW_DOWN_FLAGS
])

AC_DEFUN([ZFS_AC_KERNEL_FOLLOW_DOWN], [
	ZFS_AC_KERNEL_FOLLOW_DOWN_ONE
	ZFS_AC_KERNEL_FOLLOW_DOWN_FLAGS
])
