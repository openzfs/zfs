// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2026, Walter McKelvie.
 */

#ifndef	_SYS_ZIO_CRYPT_OS_KCAPI_H
#define	_SYS_ZIO_CRYPT_OS_KCAPI_H

/* For crypto_key_t; the ICP is still used for everything else. */
#include <sys/crypto/api.h>

struct crypto_aead;
struct crypto_shash;
struct shash_desc;

typedef struct zio_crypt_session {
	struct crypto_aead	*zs_aead;
	struct crypto_shash	*zs_shash;
} zio_crypt_session_t;

typedef struct zio_crypt_hmac {
	struct shash_desc	*zh_desc;
} zio_crypt_hmac_t;

#endif
