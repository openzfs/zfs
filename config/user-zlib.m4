dnl #
dnl # Check for zlib
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_ZLIB], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(ZLIB, [zlib], [zlib.h], [], [z], [compress2 uncompress crc32], [], [
	    AC_MSG_FAILURE([*** zlib-devel package required])
	])
])
