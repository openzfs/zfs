/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

#ifndef	_SYS_CRYPTO_SPI_H
#define	_SYS_CRYPTO_SPI_H

/*
 * CSPI: Cryptographic Service Provider Interface.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef CONSTIFY_PLUGIN
#define	__no_const __attribute__((no_const))
#else
#define	__no_const
#endif /* CONSTIFY_PLUGIN */

/*
 * Context templates can be used to by providers to pre-process
 * keying material, such as key schedules. They are allocated by
 * a provider create_ctx_template(9E) entry point, and passed
 * as argument to initialization and atomic provider entry points.
 */
typedef void *crypto_spi_ctx_template_t;

/*
 * The context structure is passed from the kernel to a provider.
 * It contains the information needed to process a multi-part or
 * single part operation. The context structure is not used
 * by atomic operations.
 *
 * Parameters needed to perform a cryptographic operation, such
 * as keys, mechanisms, input and output buffers, are passed
 * as separate arguments to Provider routines.
 */
typedef struct crypto_ctx {
	void			*cc_provider_private;	/* owned by provider */
	void			*cc_framework_private;	/* owned by framework */
} crypto_ctx_t;

/*
 * The crypto_digest_ops structure contains pointers to digest
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_digest_ops {
	int (*digest_init)(crypto_ctx_t *, crypto_mechanism_t *);
	int (*digest)(crypto_ctx_t *, crypto_data_t *, crypto_data_t *);
	int (*digest_update)(crypto_ctx_t *, crypto_data_t *);
	int (*digest_key)(crypto_ctx_t *, crypto_key_t *);
	int (*digest_final)(crypto_ctx_t *, crypto_data_t *);
	int (*digest_atomic)(crypto_mechanism_t *, crypto_data_t *,
	    crypto_data_t *);
} __no_const crypto_digest_ops_t;

/*
 * The crypto_cipher_ops structure contains pointers to encryption
 * and decryption operations for cryptographic providers.  It is
 * passed through the crypto_ops(9S) structure when providers register
 * with the kernel using crypto_register_provider(9F).
 */
typedef struct crypto_cipher_ops {
	int (*encrypt_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t);
	int (*encrypt)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *);
	int (*encrypt_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *);
	int (*encrypt_final)(crypto_ctx_t *,
	    crypto_data_t *);
	int (*encrypt_atomic)(crypto_mechanism_t *, crypto_key_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);

	int (*decrypt_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t);
	int (*decrypt)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *);
	int (*decrypt_update)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *);
	int (*decrypt_final)(crypto_ctx_t *,
	    crypto_data_t *);
	int (*decrypt_atomic)(crypto_mechanism_t *, crypto_key_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
} __no_const crypto_cipher_ops_t;

/*
 * The crypto_mac_ops structure contains pointers to MAC
 * operations for cryptographic providers.  It is passed through
 * the crypto_ops(9S) structure when providers register with the
 * kernel using crypto_register_provider(9F).
 */
typedef struct crypto_mac_ops {
	int (*mac_init)(crypto_ctx_t *,
	    crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t);
	int (*mac)(crypto_ctx_t *,
	    crypto_data_t *, crypto_data_t *);
	int (*mac_update)(crypto_ctx_t *,
	    crypto_data_t *);
	int (*mac_final)(crypto_ctx_t *,
	    crypto_data_t *);
	int (*mac_atomic)(crypto_mechanism_t *, crypto_key_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
	int (*mac_verify_atomic)(crypto_mechanism_t *, crypto_key_t *,
	    crypto_data_t *, crypto_data_t *, crypto_spi_ctx_template_t);
} __no_const crypto_mac_ops_t;

/*
 * The crypto_ctx_ops structure contains points to context and context
 * templates management operations for cryptographic providers. It is
 * passed through the crypto_ops(9S) structure when providers register
 * with the kernel using crypto_register_provider(9F).
 */
typedef struct crypto_ctx_ops {
	int (*create_ctx_template)(crypto_mechanism_t *, crypto_key_t *,
	    crypto_spi_ctx_template_t *, size_t *);
	int (*free_context)(crypto_ctx_t *);
} __no_const crypto_ctx_ops_t;

/*
 * The crypto_ops(9S) structure contains the structures containing
 * the pointers to functions implemented by cryptographic providers.
 * It is specified as part of the crypto_provider_info(9S)
 * supplied by a provider when it registers with the kernel
 * by calling crypto_register_provider(9F).
 */
typedef struct crypto_ops {
	const crypto_digest_ops_t			*co_digest_ops;
	const crypto_cipher_ops_t			*co_cipher_ops;
	const crypto_mac_ops_t			*co_mac_ops;
	const crypto_ctx_ops_t			*co_ctx_ops;
} crypto_ops_t;

/*
 * The mechanism info structure crypto_mech_info_t contains a function group
 * bit mask cm_func_group_mask. This field, of type crypto_func_group_t,
 * specifies the provider entry point that can be used a particular
 * mechanism. The function group mask is a combination of the following values.
 */

typedef uint32_t crypto_func_group_t;


#define	CRYPTO_FG_ENCRYPT		0x00000001 /* encrypt_init() */
#define	CRYPTO_FG_DECRYPT		0x00000002 /* decrypt_init() */
#define	CRYPTO_FG_DIGEST		0x00000004 /* digest_init() */
#define	CRYPTO_FG_MAC			0x00001000 /* mac_init() */
#define	CRYPTO_FG_ENCRYPT_ATOMIC	0x00008000 /* encrypt_atomic() */
#define	CRYPTO_FG_DECRYPT_ATOMIC	0x00010000 /* decrypt_atomic() */
#define	CRYPTO_FG_MAC_ATOMIC		0x00020000 /* mac_atomic() */
#define	CRYPTO_FG_DIGEST_ATOMIC		0x00040000 /* digest_atomic() */

/*
 * Maximum length of the pi_provider_description field of the
 * crypto_provider_info structure.
 */
#define	CRYPTO_PROVIDER_DESCR_MAX_LEN	64


/*
 * The crypto_mech_info structure specifies one of the mechanisms
 * supported by a cryptographic provider. The pi_mechanisms field of
 * the crypto_provider_info structure contains a pointer to an array
 * of crypto_mech_info's.
 */
typedef struct crypto_mech_info {
	crypto_mech_name_t	cm_mech_name;
	crypto_mech_type_t	cm_mech_number;
	crypto_func_group_t	cm_func_group_mask;
} crypto_mech_info_t;

/*
 * crypto_kcf_provider_handle_t is a handle allocated by the kernel.
 * It is returned after the provider registers with
 * crypto_register_provider(), and must be specified by the provider
 * when calling crypto_unregister_provider(), and
 * crypto_provider_notification().
 */
typedef uint_t crypto_kcf_provider_handle_t;

/*
 * Provider information. Passed as argument to crypto_register_provider(9F).
 * Describes the provider and its capabilities.
 */
typedef struct crypto_provider_info {
	const char				*pi_provider_description;
	const crypto_ops_t			*pi_ops_vector;
	uint_t				pi_mech_list_count;
	const crypto_mech_info_t		*pi_mechanisms;
} crypto_provider_info_t;

/*
 * Functions exported by Solaris to cryptographic providers. Providers
 * call these functions to register and unregister, notify the kernel
 * of state changes, and notify the kernel when a asynchronous request
 * completed.
 */
extern int crypto_register_provider(const crypto_provider_info_t *,
		crypto_kcf_provider_handle_t *);
extern int crypto_unregister_provider(crypto_kcf_provider_handle_t);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_SPI_H */
