dnl #
dnl # Check for libuuid
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBUUID], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBUUID, [uuid], [uuid/uuid.h], [], [uuid], [uuid_generate uuid_is_null], [], [
	    AC_MSG_FAILURE([*** libuuid-devel package required])
	])
])
