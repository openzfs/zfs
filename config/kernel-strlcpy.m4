dnl #
dnl # 6.8 removed strlcpy.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_STRLCPY], [
	ZFS_LINUX_TEST_SRC([kernel_has_strlcpy], [
		#include <linux/string.h>
	], [
		const char *src = "goodbye";
		char dst[32];
		size_t len;
		len = strlcpy(dst, src, sizeof (dst));
	])
])

AC_DEFUN([ZFS_AC_KERNEL_STRLCPY], [
	AC_MSG_CHECKING([whether strlcpy() exists])
	ZFS_LINUX_TEST_RESULT([kernel_has_strlcpy], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_KERNEL_STRLCPY, 1,
			[strlcpy() exists])
	], [
		AC_MSG_RESULT([no])
	])
])
