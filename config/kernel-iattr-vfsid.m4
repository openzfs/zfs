dnl #
dnl # 6.0 API change
dnl # struct iattr has two unions for the uid and gid
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_IATTR_VFSID], [
	ZFS_LINUX_TEST_SRC([iattr_vfsid], [
		#include <linux/fs.h>
	], [
		struct iattr ia;
		ia.ia_vfsuid = (vfsuid_t){0};
		ia.ia_vfsgid = (vfsgid_t){0};
	])
])

AC_DEFUN([ZFS_AC_KERNEL_IATTR_VFSID], [
	AC_MSG_CHECKING([whether iattr->ia_vfsuid and iattr->ia_vfsgid exist])
	ZFS_LINUX_TEST_RESULT([iattr_vfsid], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IATTR_VFSID, 1,
		    [iattr->ia_vfsuid and iattr->ia_vfsgid exist])
	],[
		AC_MSG_RESULT(no)
	])
])
