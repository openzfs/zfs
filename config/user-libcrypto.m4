dnl #
dnl # Check for libcrypto. Used for userspace password derivation via PBKDF2.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBCRYPTO], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBCRYPTO, [libcrypto], [openssl/evp.h], [], [crypto], [PKCS5_PBKDF2_HMAC_SHA1], [], [
		AC_MSG_FAILURE([
		*** evp.h missing, libssl-devel package required])])
])
