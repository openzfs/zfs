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

#ifndef _SYS_CRYPTO_OPS_IMPL_H
#define	_SYS_CRYPTO_OPS_IMPL_H

/*
 * Scheduler internal structures.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/common.h>

/*
 * The parameters needed for each function group are batched
 * in one structure. This is much simpler than having a
 * separate structure for each function.
 *
 * In some cases, a field is generically named to keep the
 * structure small. The comments indicate these cases.
 */
typedef struct kcf_digest_ops_params {
	crypto_session_id_t	do_sid;
	crypto_mech_type_t	do_framework_mechtype;
	crypto_mechanism_t	do_mech;
	crypto_data_t		*do_data;
	crypto_data_t		*do_digest;
	crypto_key_t		*do_digest_key;	/* Argument for digest_key() */
} kcf_digest_ops_params_t;

typedef struct kcf_mac_ops_params {
	crypto_session_id_t		mo_sid;
	crypto_mech_type_t		mo_framework_mechtype;
	crypto_mechanism_t		mo_mech;
	crypto_key_t			*mo_key;
	crypto_data_t			*mo_data;
	crypto_data_t			*mo_mac;
	crypto_spi_ctx_template_t	mo_templ;
} kcf_mac_ops_params_t;

typedef struct kcf_encrypt_ops_params {
	crypto_session_id_t		eo_sid;
	crypto_mech_type_t		eo_framework_mechtype;
	crypto_mechanism_t		eo_mech;
	crypto_key_t			*eo_key;
	crypto_data_t			*eo_plaintext;
	crypto_data_t			*eo_ciphertext;
	crypto_spi_ctx_template_t	eo_templ;
} kcf_encrypt_ops_params_t;

typedef struct kcf_decrypt_ops_params {
	crypto_session_id_t		dop_sid;
	crypto_mech_type_t		dop_framework_mechtype;
	crypto_mechanism_t		dop_mech;
	crypto_key_t			*dop_key;
	crypto_data_t			*dop_ciphertext;
	crypto_data_t			*dop_plaintext;
	crypto_spi_ctx_template_t	dop_templ;
} kcf_decrypt_ops_params_t;

/*
 * The operation type within a function group.
 */
typedef enum kcf_op_type {
	/* common ops for all mechanisms */
	KCF_OP_INIT = 1,
	KCF_OP_SINGLE,	/* pkcs11 sense. So, INIT is already done */
	KCF_OP_UPDATE,
	KCF_OP_FINAL,
	KCF_OP_ATOMIC,

	/* digest_key op */
	KCF_OP_DIGEST_KEY,

	/* mac specific op */
	KCF_OP_MAC_VERIFY_ATOMIC,

	/* mac/cipher specific op */
	KCF_OP_MAC_VERIFY_DECRYPT_ATOMIC,
} kcf_op_type_t;

/*
 * The operation groups that need wrapping of parameters. This is somewhat
 * similar to the function group type in spi.h except that this also includes
 * all the functions that don't have a mechanism.
 *
 * The wrapper macros should never take these enum values as an argument.
 * Rather, they are assigned in the macro itself since they are known
 * from the macro name.
 */
typedef enum kcf_op_group {
	KCF_OG_DIGEST = 1,
	KCF_OG_MAC,
	KCF_OG_ENCRYPT,
	KCF_OG_DECRYPT,
} kcf_op_group_t;

/*
 * The kcf_op_type_t enum values used here should be only for those
 * operations for which there is a k-api routine in sys/crypto/api.h.
 */
#define	IS_INIT_OP(ftype)	((ftype) == KCF_OP_INIT)
#define	IS_SINGLE_OP(ftype)	((ftype) == KCF_OP_SINGLE)
#define	IS_UPDATE_OP(ftype)	((ftype) == KCF_OP_UPDATE)
#define	IS_FINAL_OP(ftype)	((ftype) == KCF_OP_FINAL)
#define	IS_ATOMIC_OP(ftype)	( \
	(ftype) == KCF_OP_ATOMIC || (ftype) == KCF_OP_MAC_VERIFY_ATOMIC)

/*
 * Keep the parameters associated with a request around.
 * We need to pass them to the SPI.
 */
typedef struct kcf_req_params {
	kcf_op_group_t		rp_opgrp;
	kcf_op_type_t		rp_optype;

	union {
		kcf_digest_ops_params_t		digest_params;
		kcf_mac_ops_params_t		mac_params;
		kcf_encrypt_ops_params_t	encrypt_params;
		kcf_decrypt_ops_params_t	decrypt_params;
	} rp_u;
} kcf_req_params_t;


/*
 * The ioctl/k-api code should bundle the parameters into a kcf_req_params_t
 * structure before calling a scheduler routine. The following macros are
 * available for that purpose.
 *
 * For the most part, the macro arguments closely correspond to the
 * function parameters. In some cases, we use generic names. The comments
 * for the structure should indicate these cases.
 */
#define	KCF_WRAP_DIGEST_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_data, _digest) {						\
	kcf_digest_ops_params_t *dops = &(req)->rp_u.digest_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_DIGEST;				\
	(req)->rp_optype = ftype;					\
	dops->do_sid = _sid;						\
	if (mechp != NULL) {						\
		dops->do_mech = *mechp;					\
		dops->do_framework_mechtype = mechp->cm_type;		\
	}								\
	dops->do_digest_key = _key;					\
	dops->do_data = _data;						\
	dops->do_digest = _digest;					\
}

#define	KCF_WRAP_MAC_OPS_PARAMS(req, ftype, _sid, _mech, _key,		\
	_data, _mac, _templ) {						\
	kcf_mac_ops_params_t *mops = &(req)->rp_u.mac_params;		\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_MAC;					\
	(req)->rp_optype = ftype;					\
	mops->mo_sid = _sid;						\
	if (mechp != NULL) {						\
		mops->mo_mech = *mechp;					\
		mops->mo_framework_mechtype = mechp->cm_type;		\
	}								\
	mops->mo_key = _key;						\
	mops->mo_data = _data;						\
	mops->mo_mac = _mac;						\
	mops->mo_templ = _templ;					\
}

#define	KCF_WRAP_ENCRYPT_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_plaintext, _ciphertext, _templ) {				\
	kcf_encrypt_ops_params_t *cops = &(req)->rp_u.encrypt_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_ENCRYPT;				\
	(req)->rp_optype = ftype;					\
	cops->eo_sid = _sid;						\
	if (mechp != NULL) {						\
		cops->eo_mech = *mechp;					\
		cops->eo_framework_mechtype = mechp->cm_type;		\
	}								\
	cops->eo_key = _key;						\
	cops->eo_plaintext = _plaintext;				\
	cops->eo_ciphertext = _ciphertext;				\
	cops->eo_templ = _templ;					\
}

#define	KCF_WRAP_DECRYPT_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_ciphertext, _plaintext, _templ) {				\
	kcf_decrypt_ops_params_t *cops = &(req)->rp_u.decrypt_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_DECRYPT;				\
	(req)->rp_optype = ftype;					\
	cops->dop_sid = _sid;						\
	if (mechp != NULL) {						\
		cops->dop_mech = *mechp;				\
		cops->dop_framework_mechtype = mechp->cm_type;		\
	}								\
	cops->dop_key = _key;						\
	cops->dop_ciphertext = _ciphertext;				\
	cops->dop_plaintext = _plaintext;				\
	cops->dop_templ = _templ;					\
}

#define	KCF_SET_PROVIDER_MECHNUM(fmtype, pd, mechp)			\
	(mechp)->cm_type =						\
	    KCF_TO_PROV_MECHNUM(pd, fmtype);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_OPS_IMPL_H */
