dnl #
dnl # Check for libblkid.  Basic support for detecting ZFS pools
dnl # has existing in blkid since 2008.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBBLKID], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBBLKID, [blkid], [blkid/blkid.h], [], [blkid], [], [], [
		AC_MSG_FAILURE([
		*** blkid.h missing, libblkid-devel package required])])
])
