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
 * Copyright (c) 2009 Sun Microsystems, Inc.  All rights reserved.
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifndef	_SHA2_IMPL_H
#define	_SHA2_IMPL_H

#include <sys/sha2.h>

#ifdef __cplusplus
extern "C" {
#endif

/* transform function definition */
typedef void (*sha256_f)(uint32_t state[8], const void *data, size_t blks);
typedef void (*sha512_f)(uint64_t state[8], const void *data, size_t blks);

/* needed for checking valid implementations */
typedef boolean_t (*sha2_is_supported_f)(void);

typedef struct {
	const char *name;
	sha256_f transform;
	sha2_is_supported_f is_supported;
} sha256_ops_t;

typedef struct {
	const char *name;
	sha512_f transform;
	sha2_is_supported_f is_supported;
} sha512_ops_t;

extern const sha256_ops_t *sha256_get_ops(void);
extern const sha512_ops_t *sha512_get_ops(void);

typedef enum {
	SHA1_TYPE,
	SHA256_TYPE,
	SHA384_TYPE,
	SHA512_TYPE
} sha2_mech_t;

/*
 * Context for SHA2 mechanism.
 */
typedef struct sha2_ctx {
	sha2_mech_type_t	sc_mech_type;	/* type of context */
	SHA2_CTX		sc_sha2_ctx;	/* SHA2 context */
} sha2_ctx_t;

/*
 * Context for SHA2 HMAC and HMAC GENERAL mechanisms.
 */
typedef struct sha2_hmac_ctx {
	sha2_mech_type_t	hc_mech_type;	/* type of context */
	uint32_t		hc_digest_len;	/* digest len in bytes */
	SHA2_CTX		hc_icontext;	/* inner SHA2 context */
	SHA2_CTX		hc_ocontext;	/* outer SHA2 context */
} sha2_hmac_ctx_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SHA2_IMPL_H */
