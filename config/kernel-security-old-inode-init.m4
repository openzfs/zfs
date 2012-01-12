dnl #
dnl # Linux 3.2 API Change
dnl #
dnl # The security_inode_init_security() function was renamed to
dnl # security_old_inode_init_security and the symbol was reimplemented.
dnl #
dnl # The new security_inode_init_security() function never returns ENOTSUPP
dnl # and *name, *value, or len may be uninitialized on a successful return.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SECURITY_OLD_INODE_INIT_SECURITY], [
	AC_MSG_CHECKING([whether security_old_inode_init_security() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/security.h>
	],[
		security_old_inode_init_security(NULL,NULL,NULL,NULL,NULL,NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SECURITY_OLD_INODE_INIT_SECURITY, 1,
		  [security_old_inode_init_security() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
