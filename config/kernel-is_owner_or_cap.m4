dnl #
dnl # 2.6.39 API change,
dnl # The is_owner_or_cap() macro was renamed to inode_owner_or_capable(),
dnl # This is used for permission checks in the xattr and file attribute call
dnl # paths.
dnl #
dnl # 5.12 API change,
dnl # inode_owner_or_capable() now takes struct user_namespace *
dnl # to support idmapped mounts
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_OWNER_OR_CAPABLE], [
	ZFS_LINUX_TEST_SRC([inode_owner_or_capable], [
		#include <linux/fs.h>
	],[
		struct inode *ip = NULL;
		(void) inode_owner_or_capable(ip);
	])

	ZFS_LINUX_TEST_SRC([inode_owner_or_capable_userns], [
		#include <linux/fs.h>
	],[
		struct inode *ip = NULL;
		(void) inode_owner_or_capable(&init_user_ns, ip);
	])

	ZFS_LINUX_TEST_SRC([inode_owner_or_capable_mnt_idmap], [
		#include <linux/fs.h>
		#include <linux/mnt_idmapping.h>
	],[
		struct inode *ip = NULL;
		(void) inode_owner_or_capable(&nop_mnt_idmap, ip);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_OWNER_OR_CAPABLE], [
	AC_MSG_CHECKING([whether inode_owner_or_capable() exists])
	ZFS_LINUX_TEST_RESULT([inode_owner_or_capable], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_OWNER_OR_CAPABLE, 1,
		    [inode_owner_or_capable() exists])
	], [
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING(
		    [whether inode_owner_or_capable() takes user_ns])
		ZFS_LINUX_TEST_RESULT([inode_owner_or_capable_userns], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_INODE_OWNER_OR_CAPABLE_USERNS, 1,
			    [inode_owner_or_capable() takes user_ns])
		],[
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING(
			    [whether inode_owner_or_capable() takes mnt_idmap])
			ZFS_LINUX_TEST_RESULT([inode_owner_or_capable_mnt_idmap], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_INODE_OWNER_OR_CAPABLE_IDMAP, 1,
				    [inode_owner_or_capable() takes mnt_idmap])
			], [
				ZFS_LINUX_TEST_ERROR([capability])
			])
		])
	])
])
