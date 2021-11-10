dnl #
dnl # 3.2 API change
dnl # The security_inode_init_security() API has been changed to include
dnl # a filesystem specific callback to write security extended attributes.
dnl # This was done to support the initialization of multiple LSM xattrs
dnl # and the EVM xattr.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SECURITY_INODE_INIT_SECURITY_CALLBACK], [
	ZFS_LINUX_TEST_SRC([security_inode_init_security], [
		#include <linux/security.h>
	],[
		struct inode *ip __attribute__ ((unused)) = NULL;
		struct inode *dip __attribute__ ((unused)) = NULL;
		const struct qstr *str __attribute__ ((unused)) = NULL;
		initxattrs func __attribute__ ((unused)) = NULL;

		security_inode_init_security(ip, dip, str, func, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SECURITY_INODE_INIT_SECURITY_CALLBACK], [
	AC_MSG_CHECKING([whether security_inode_init_security wants callback])
	ZFS_LINUX_TEST_RESULT([security_inode_init_security], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([security_inode_init_security callback])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SECURITY_INODE], [
	ZFS_AC_KERNEL_SRC_SECURITY_INODE_INIT_SECURITY_CALLBACK
])

AC_DEFUN([ZFS_AC_KERNEL_SECURITY_INODE], [
	ZFS_AC_KERNEL_SECURITY_INODE_INIT_SECURITY_CALLBACK
])
