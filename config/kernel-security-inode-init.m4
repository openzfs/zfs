dnl #
dnl # 2.6.39 API change
dnl # The security_inode_init_security() function now takes an additional
dnl # qstr argument which must be passed in from the dentry if available.
dnl # Passing a NULL is safe when no qstr is available the relevant
dnl # security checks will just be skipped.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_6ARGS_SECURITY_INODE_INIT_SECURITY], [
	AC_MSG_CHECKING([whether security_inode_init_security wants 6 args])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/security.h>
	],[
		security_inode_init_security(NULL,NULL,NULL,NULL,NULL,NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_6ARGS_SECURITY_INODE_INIT_SECURITY, 1,
		          [security_inode_init_security wants 6 args])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
