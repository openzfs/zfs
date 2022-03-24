dnl #
dnl # Check for libaio - only used for mmap_libaio test cases.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBAIO], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBAIO, [], [libaio.h], [], [aio], [], [user_libaio=yes], [user_libaio=no])
])
