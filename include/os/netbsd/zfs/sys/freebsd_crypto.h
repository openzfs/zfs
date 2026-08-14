// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_ZFS_FREEBSD_CRYPTO_H
#define	_ZFS_FREEBSD_CRYPTO_H

#include <sys/errno.h>
#include <sys/sha2.h>

#undef SUN_CKM_AES_CCM
#undef SUN_CKM_AES_GCM
#undef SUN_CKM_SHA512_HMAC
#define	SUN_CKM_AES_CCM		"CKM_AES_CCM"
#define	SUN_CKM_AES_GCM		"CKM_AES_GCM"
#define	SUN_CKM_SHA512_HMAC	"CKM_SHA512_HMAC"

typedef const char *crypto_mech_name_t;
#define	CRYPTO_BITS2BYTES(n)	((n) == 0 ? 0 : (((n) - 1) >> 3) + 1)
#define	CRYPTO_BYTES2BITS(n)	((n) << 3)

struct zio_crypt_info;

typedef struct freebsd_crypt_session {
	int	_unused;
} freebsd_crypt_session_t;

typedef void *crypto_mechanism_t;
typedef void *crypto_ctx_template_t;

typedef struct crypto_key {
	void	*ck_data;
	size_t	ck_length;
} crypto_key_t;

typedef struct hmac_ctx {
	SHA2_CTX	innerctx;
	SHA2_CTX	outerctx;
} *crypto_context_t;

void crypto_mac(const crypto_key_t *key, const void *in_data,
    size_t in_data_size, void *out_data, size_t out_data_size);
void crypto_mac_init(struct hmac_ctx *ctx, const crypto_key_t *key);
void crypto_mac_update(struct hmac_ctx *ctx, const void *data,
    size_t data_size);
void crypto_mac_final(struct hmac_ctx *ctx, void *out_data,
    size_t out_data_size);

int freebsd_crypt_newsession(freebsd_crypt_session_t *sessp,
    const struct zio_crypt_info *, crypto_key_t *);
void freebsd_crypt_freesession(freebsd_crypt_session_t *sessp);
int freebsd_crypt_uio(boolean_t, freebsd_crypt_session_t *,
    const struct zio_crypt_info *, zfs_uio_t *, crypto_key_t *, uint8_t *,
    size_t, size_t);

#endif /* _ZFS_FREEBSD_CRYPTO_H */
