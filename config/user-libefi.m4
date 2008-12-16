dnl #
dnl # Check for libefi
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBEFI], [
	AC_CHECK_LIB([efi], [efi_alloc_and_init],
		[AC_DEFINE([HAVE_LIBEFI], 1,
		[Define to 1 if 'libefi' library available])])
])
