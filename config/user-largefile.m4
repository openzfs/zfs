dnl #
dnl # ZFS_AC_CONFIG_USER_LARGEFILE
dnl #
dnl # Ensure off_t is 64-bit for large file support in userspace.
dnl # This is required for OpenZFS to handle files larger than 2GB.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LARGEFILE], [
	AC_SYS_LARGEFILE
	AC_CHECK_SIZEOF([off_t])

	AC_MSG_CHECKING([for 64-bit off_t])
	AS_IF([test "$ac_cv_sizeof_off_t" -ne 8], [
		AC_MSG_RESULT([no, $ac_cv_sizeof_off_t bytes])
		AC_MSG_FAILURE([
*** OpenZFS userspace requires 64-bit off_t support for large files.
*** Please ensure your system supports large file operations.
*** Current off_t size: $ac_cv_sizeof_off_t bytes])
	], [
		AC_MSG_RESULT([yes, $ac_cv_sizeof_off_t bytes])
	])
])
