// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
