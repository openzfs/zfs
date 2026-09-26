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
dnl # The test exercises every interface used by the kernel crypto API
dnl # backend (zio_crypt_os_kcapi.c), so that a future API change makes
dnl # this check fail and the ICP backend be used, rather than breaking
dnl # the module build.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CRYPTO], [
	ZFS_LINUX_TEST_SRC([kernel_crypto], [
		#include <linux/mm.h>
		#include <linux/vmalloc.h>
		#include <linux/scatterlist.h>
		#include <crypto/aead.h>
		#include <crypto/hash.h>
	], [
		static struct shash_desc desc_storage;
		struct crypto_aead *aead;
		struct aead_request *req;
		struct crypto_shash *shash;
		struct shash_desc *desc = &desc_storage;
		struct scatterlist sg[2];
		u8 buf[16] = {0};
		int err;
		DECLARE_CRYPTO_WAIT(wait);

		aead = crypto_alloc_aead("gcm(aes)", 0, 0);
		(void) crypto_aead_setkey(aead, buf, sizeof (buf));
		(void) crypto_aead_setauthsize(aead, sizeof (buf));
		(void) crypto_aead_ivsize(aead);
		req = aead_request_alloc(aead, GFP_KERNEL);
		aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
		    CRYPTO_TFM_REQ_MAY_SLEEP, crypto_req_done, &wait);
		sg_init_table(sg, 2);
		sg_set_buf(&sg[0], buf, sizeof (buf));
		sg_set_page(&sg[1], vmalloc_to_page(buf), sizeof (buf),
		    offset_in_page(buf));
		(void) sg_next(sg);
		(void) is_vmalloc_addr(buf);
		aead_request_set_ad(req, 0);
		aead_request_set_crypt(req, sg, sg, sizeof (buf), buf);
		err = crypto_wait_req(crypto_aead_encrypt(req), &wait);
		err = crypto_wait_req(crypto_aead_decrypt(req), &wait);
		(void) err;
		aead_request_free(req);
		crypto_free_aead(aead);

		shash = crypto_alloc_shash("hmac(sha512)", 0, 0);
		(void) crypto_shash_setkey(shash, buf, sizeof (buf));
		(void) crypto_shash_descsize(shash);
		{
			SHASH_DESC_ON_STACK(sdesc, shash);
			sdesc->tfm = shash;
			(void) crypto_shash_digest(sdesc, buf, sizeof (buf), buf);
			shash_desc_zero(sdesc);
		}
		desc->tfm = shash;
		(void) crypto_shash_init(desc);
		(void) crypto_shash_update(desc, buf, sizeof (buf));
		(void) crypto_shash_final(desc, buf);
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
