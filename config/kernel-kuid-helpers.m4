dnl #
dnl # 3.5 API change,
dnl # Since usernamespaces were introduced in kernel version 3.5, it
dnl # became necessary to go through one more level of indirection
dnl # when dealing with uid/gid - namely the kuid type.
dnl #
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KUID_HELPERS], [
	ZFS_LINUX_TEST_SRC([i_uid_read], [
		#include <linux/fs.h>
	],[
		struct inode *ip = NULL;
		(void) i_uid_read(ip);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KUID_HELPERS], [
	AC_MSG_CHECKING([whether i_(uid|gid)_(read|write) exist])
	ZFS_LINUX_TEST_RESULT([i_uid_read], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([i_uid_read])
	])
])
