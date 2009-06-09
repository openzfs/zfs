dnl #
dnl # Check for zlib
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_ZLIB], [
	AC_CHECK_HEADER([zlib.h], [], [AC_MSG_ERROR([
		*** zlib.h missing, the zlib-devel package is required])])
	AC_CHECK_LIB([z], [compress2], [], [AC_MSG_ERROR([
		*** compress2() missing, the zlib-devel package is required])])
	AC_CHECK_LIB([z], [uncompress], [], [AC_MSG_ERROR([
		*** uncompress() missing, the zlib-devel package is required])])
])
