dnl #
dnl # Check for libcrypto. Used for userspace password derivation via PBKDF2.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBCRYPTO], [
#	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBCRYPTO, [libcrypto], [openssl/evp.h], [], [crypto], [PKCS5_PBKDF2_HMAC_SHA1], [], [
#		AC_MSG_FAILURE([
#		*** evp.h missing, libssl-devel package required])])

ZFS_AC_FIND_SYSTEM_LIBRARY(LIBCRYPTO_SSL, [libcrypto], [openssl/evp.h], [], [crypto], [PKCS5_PBKDF2_HMAC_SHA1], [], [
	AC_MSG_FAILURE([*** evp.h missing, libssl-devel package required])])
ZFS_AC_FIND_SYSTEM_LIBRARY(LIBCRYPTO_ARGON2, [libargon2], [argon2.h], [], [argon2], [argon2id_hash_raw], [], [
	AC_MSG_FAILURE([*** libargon2-dev package required])])

LIBCRYPTO_CFLAGS="$LIBCRYPTO_SSL_CFLAGS $LIBCRYPTO_ARGON2_CFLAGS"
LIBCRYPTO_LIBS="$LIBCRYPTO_SSL_LIBS $LIBCRYPTO_ARGON2_LIBS"
AC_SUBST(LIBCRYPTO_CFLAGS, [])
AC_SUBST(LIBCRYPTO_LIBS, [])
])

