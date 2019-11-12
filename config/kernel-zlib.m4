dnl #
dnl # 2.6.39 API compat,
dnl
dnl # The function zlib_deflate_workspacesize() now take 2 arguments.
dnl # This was done to avoid always having to allocate the maximum size
dnl # workspace (268K).  The caller can now specific the windowBits and
dnl # memLevel compression parameters to get a smaller workspace.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE], [
	ZFS_LINUX_TEST_SRC([2args_zlib_deflate_workspacesize], [
		#include <linux/zlib.h>
	],[
		return zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE], [
	AC_MSG_CHECKING([whether zlib_deflate_workspacesize() wants 2 args])
	ZFS_LINUX_TEST_RESULT([2args_zlib_deflate_workspacesize], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE, 1,
		    [zlib_deflate_workspacesize() wants 2 args])
	],[
		ZFS_LINUX_TEST_ERROR([zlib_deflate_workspacesize()])
	])
])
