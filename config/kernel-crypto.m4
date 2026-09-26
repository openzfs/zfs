dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Check for the in-kernel crypto API (AEAD and shash).
dnl #
dnl # This can only succeed when the kernel exports these symbols with a
dnl # license compatible with the module license. As of Linux 7.1.5 they
dnl # are exported GPL-only, so with the standard CDDL module license this
dnl # only has an effect when built against a patched kernel that exports
dnl # them non-GPL (or when the module license is overridden).
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CRYPTO], [
	ZFS_LINUX_TEST_SRC([kernel_crypto], [
		#include <linux/scatterlist.h>
		#include <crypto/aead.h>
		#include <crypto/hash.h>
	], [
		static struct aead_request req_storage;
		static struct shash_desc desc_storage;
		struct crypto_aead *aead;
		struct aead_request *req = &req_storage;
		struct crypto_shash *shash;
		struct shash_desc *desc = &desc_storage;
		struct crypto_wait wait;
		u8 buf[16] = {0};

		aead = crypto_alloc_aead("gcm(aes)", 0, 0);
		(void) crypto_aead_setkey(aead, buf, sizeof (buf));
		(void) crypto_aead_setauthsize(aead, sizeof (buf));
		aead_request_set_callback(req, 0, crypto_req_done, &wait);
		(void) crypto_aead_encrypt(req);
		(void) crypto_aead_decrypt(req);
		crypto_free_aead(aead);

		shash = crypto_alloc_shash("hmac(sha512)", 0, 0);
		(void) crypto_shash_setkey(shash, buf, sizeof (buf));
		(void) crypto_shash_init(desc);
		(void) crypto_shash_update(desc, buf, sizeof (buf));
		(void) crypto_shash_final(desc, buf);
		(void) crypto_shash_digest(desc, buf, sizeof (buf), buf);
		crypto_free_shash(shash);
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_CRYPTO], [
	AC_MSG_CHECKING([whether kernel crypto API is available])
	ZFS_LINUX_TEST_RESULT([kernel_crypto_license], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_CRYPTO, 1,
		    [kernel crypto API is available])
	], [
		AC_MSG_RESULT(no)
	])
])
