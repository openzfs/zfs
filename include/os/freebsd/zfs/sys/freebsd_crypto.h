// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2018 Sean Eric Fagan <sef@ixsystems.com>
 * Portions Copyright (c) 2005-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Portions of this file were taken from GELI's implementation of hmac.
 *
 * $FreeBSD$
 */

#ifndef _ZFS_FREEBSD_CRYPTO_H
#define	_ZFS_FREEBSD_CRYPTO_H

#include <sys/errno.h>
#include <sys/mutex.h>
#include <opencrypto/cryptodev.h>
#include <crypto/sha2/sha256.h>
#include <crypto/sha2/sha512.h>

#define	SUN_CKM_AES_CCM	"CKM_AES_CCM"
#define	SUN_CKM_AES_GCM	"CKM_AES_GCM"
#define	SUN_CKM_SHA512_HMAC	"CKM_SHA512_HMAC"
#define	SUN_CKM_CHACHA20_POLY1305	"CKM_CHACHA20_POLY1305"

#define	CRYPTO_BITS2BYTES(n) ((n) == 0 ? 0 : (((n) - 1) >> 3) + 1)
#define	CRYPTO_BYTES2BITS(n) ((n) << 3)

struct zio_crypt_info;

typedef struct freebsd_crypt_session {
	struct mtx		fs_lock;
	crypto_session_t	fs_sid;
	boolean_t	fs_done;
} freebsd_crypt_session_t;

/*
 * Unused types to minimize code differences.
 */
typedef void *crypto_mechanism_t;
typedef void *crypto_ctx_template_t;
/*
 * Like the ICP crypto_key type, this only
 * supports <data, length> (the equivalent of
 * the former CRYPTO_KEY_RAW).
 */
typedef struct crypto_key {
	void	*ck_data;
	size_t	ck_length;
} crypto_key_t;

typedef struct hmac_ctx {
	SHA512_CTX	innerctx;
	SHA512_CTX	outerctx;
} *crypto_context_t;

/*
 * The only algorithm ZFS uses for hashing is SHA512_HMAC.
 */
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
