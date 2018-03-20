dnl #
dnl # Check for libssl. Used for userspace password derivation via PBKDF2.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBSSL], [
	LIBSSL=

	AC_CHECK_HEADER([openssl/evp.h], [], [AC_MSG_FAILURE([
	*** evp.h missing, libssl-devel package required])])

	AC_SUBST([LIBSSL], ["-lssl -lcrypto"])
	AC_DEFINE([HAVE_LIBSSL], 1, [Define if you have libssl])
])
