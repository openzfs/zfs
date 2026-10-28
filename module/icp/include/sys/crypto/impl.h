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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_IMPL_H
#define	_SYS_CRYPTO_IMPL_H

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/common.h>
#include <sha2/sha2_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ICP_PRIVATE	__attribute__((visibility("hidden")))

typedef void *crypto_spi_ctx_template_t;

typedef struct crypto_ctx_template_impl {
	size_t				ct_size;
	crypto_spi_ctx_template_t	ct_prov_tmpl;
} crypto_ctx_template_impl_t;

typedef enum icp_provider {
	ICP_PROVIDER_AES,
	ICP_PROVIDER_SHA2,
} icp_provider_t;

ICP_PRIVATE boolean_t icp_is_ready(void);
ICP_PRIVATE int icp_mech_to_provider(crypto_mech_type_t, icp_provider_t *,
    crypto_mech_type_t *);

ICP_PRIVATE int icp_aes_encrypt(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
ICP_PRIVATE int icp_aes_decrypt(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
ICP_PRIVATE int icp_aes_create_ctx_template(crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t *, size_t *);

ICP_PRIVATE int icp_sha2_mac_init(crypto_mechanism_t *, crypto_key_t *,
    crypto_spi_ctx_template_t, crypto_context_t *);
ICP_PRIVATE int icp_sha2_mac_update(sha2_hmac_ctx_t *, crypto_data_t *);
ICP_PRIVATE int icp_sha2_mac_final(sha2_hmac_ctx_t *, crypto_data_t *);
ICP_PRIVATE int icp_sha2_mac_atomic(crypto_mechanism_t *, crypto_key_t *,
    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
ICP_PRIVATE int icp_sha2_create_ctx_template(crypto_mechanism_t *,
    crypto_key_t *, crypto_spi_ctx_template_t *, size_t *);

ICP_PRIVATE int crypto_put_output_data(uchar_t *, crypto_data_t *, int);
ICP_PRIVATE int crypto_update_iov(void *, crypto_data_t *, crypto_data_t *,
    int (*)(void *, caddr_t, size_t, crypto_data_t *));
ICP_PRIVATE int crypto_update_uio(void *, crypto_data_t *, crypto_data_t *,
    int (*)(void *, caddr_t, size_t, crypto_data_t *));

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_IMPL_H */
