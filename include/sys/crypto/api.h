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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_API_H
#define	_SYS_CRYPTO_API_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

typedef void *crypto_context_t;
typedef void *crypto_ctx_template_t;

/*
 * Returns the mechanism type corresponding to a mechanism name.
 */
#define	CRYPTO_MECH_INVALID	((uint64_t)-1)
extern crypto_mech_type_t crypto_mech2id(const char *name);

/*
 * Create and destroy context templates.
 */
extern int crypto_create_ctx_template(crypto_mechanism_t *mech,
    crypto_key_t *key, crypto_ctx_template_t *tmpl);
extern void crypto_destroy_ctx_template(crypto_ctx_template_t tmpl);

/*
 * Single and multi-part MAC operations.
 */
extern int crypto_mac(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *mac);
extern int crypto_mac_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp);
extern int crypto_mac_update(crypto_context_t ctx, crypto_data_t *data);
extern int crypto_mac_final(crypto_context_t ctx, crypto_data_t *data);

/*
 * Single-part encryption/decryption operations.
 */
extern int crypto_encrypt(crypto_mechanism_t *mech, crypto_data_t *plaintext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *ciphertext);
extern int crypto_decrypt(crypto_mechanism_t *mech, crypto_data_t *ciphertext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *plaintext);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_API_H */
