dnl #
dnl # 6.8.x replaced strlcpy with strscpy. Check for both so we can provide
dnl # appropriate fallbacks.
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

AC_DEFUN([ZFS_AC_KERNEL_SRC_STRSCPY], [
	ZFS_LINUX_TEST_SRC([kernel_has_strscpy], [
		#include <linux/string.h>
	], [
		const char *src = "goodbye";
		char dst[32];
		ssize_t len;
		len = strscpy(dst, src, sizeof (dst));
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

AC_DEFUN([ZFS_AC_KERNEL_STRSCPY], [
	AC_MSG_CHECKING([whether strscpy() exists])
	ZFS_LINUX_TEST_RESULT([kernel_has_strscpy], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_KERNEL_STRSCPY, 1,
			[strscpy() exists])
	], [
		AC_MSG_RESULT([no])
	])
])
