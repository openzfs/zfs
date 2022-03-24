dnl #
dnl # POSIX specifies <aio.h> as part of realtime extensions,
dnl # and is missing from at least uClibc – force fallbacks there
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_AIO_H], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(AIO_H, [], [aio.h], [], [rt], [lio_listio])
])
