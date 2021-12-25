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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_IMPL_H
#define	_SYS_CRYPTO_IMPL_H

/*
 * Kernel Cryptographic Framework private implementation definitions.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	KCF_MODULE "kcf"

/*
 * Prefixes convention: structures internal to the kernel cryptographic
 * framework start with 'kcf_'. Exposed structure start with 'crypto_'.
 */

/* Provider stats. Not protected. */
typedef	struct kcf_prov_stats {
	kstat_named_t	ps_ops_total;
	kstat_named_t	ps_ops_passed;
	kstat_named_t	ps_ops_failed;
	kstat_named_t	ps_ops_busy_rval;
} kcf_prov_stats_t;

/*
 * Keep all the information needed by the scheduler from
 * this provider.
 */
typedef struct kcf_sched_info {
	/* The number of operations dispatched. */
	uint64_t	ks_ndispatches;

	/* The number of operations that failed. */
	uint64_t	ks_nfails;

	/* The number of operations that returned CRYPTO_BUSY. */
	uint64_t	ks_nbusy_rval;
} kcf_sched_info_t;

/*
 * pd_irefcnt approximates the number of inflight requests to the
 * provider. Though we increment this counter during registration for
 * other purposes, that base value is mostly same across all providers.
 * So, it is a good measure of the load on a provider when it is not
 * in a busy state. Once a provider notifies it is busy, requests
 * backup in the taskq. So, we use tq_nalloc in that case which gives
 * the number of task entries in the task queue. Note that we do not
 * acquire any locks here as it is not critical to get the exact number
 * and the lock contention may be too costly for this code path.
 */
#define	KCF_PROV_LOAD(pd)	((pd)->pd_irefcnt)

#define	KCF_PROV_INCRSTATS(pd, error)	{				\
	(pd)->pd_sched_info.ks_ndispatches++;				\
	if (error == CRYPTO_BUSY)					\
		(pd)->pd_sched_info.ks_nbusy_rval++;			\
	else if (error != CRYPTO_SUCCESS && error != CRYPTO_QUEUED)	\
		(pd)->pd_sched_info.ks_nfails++;			\
}


/*
 * The following two macros should be
 * #define	KCF_OPS_CLASSSIZE (KCF_LAST_OPSCLASS - KCF_FIRST_OPSCLASS + 2)
 * #define	KCF_MAXMECHTAB KCF_MAXCIPHER
 *
 * However, doing that would involve reorganizing the header file a bit.
 * When impl.h is broken up (bug# 4703218), this will be done. For now,
 * we hardcode these values.
 */
#define	KCF_OPS_CLASSSIZE	4
#define	KCF_MAXMECHTAB		32

/*
 * Valid values for the state of a provider. The order of
 * the elements is important.
 *
 * Routines which get a provider or the list of providers
 * should pick only those that are in KCF_PROV_READY state.
 */
typedef enum {
	KCF_PROV_ALLOCATED = 1,
	/*
	 * state < KCF_PROV_READY means the provider can not
	 * be used at all.
	 */
	KCF_PROV_READY,
	/*
	 * state > KCF_PROV_READY means the provider can not
	 * be used for new requests.
	 */
	KCF_PROV_FAILED,
	/*
	 * Threads setting the following two states should do so only
	 * if the current state < KCF_PROV_DISABLED.
	 */
	KCF_PROV_DISABLED,
	KCF_PROV_REMOVED,
	KCF_PROV_FREED
} kcf_prov_state_t;

#define	KCF_IS_PROV_USABLE(pd) ((pd)->pd_state == KCF_PROV_READY)
#define	KCF_IS_PROV_REMOVED(pd)	((pd)->pd_state >= KCF_PROV_REMOVED)

/*
 * A provider descriptor structure. There is one such structure per
 * provider. It is allocated and initialized at registration time and
 * freed when the provider unregisters.
 *
 * pd_sid:		Session ID of the provider used by kernel clients.
 *			This is valid only for session-oriented providers.
 * pd_refcnt:		Reference counter to this provider descriptor
 * pd_irefcnt:		References held by the framework internal structs
 * pd_lock:		lock protects pd_state
 * pd_state:		State value of the provider
 * pd_prov_handle:	Provider handle specified by provider
 * pd_ops_vector:	The ops vector specified by Provider
 * pd_mech_indx:	Lookup table which maps a core framework mechanism
 *			number to an index in pd_mechanisms array
 * pd_mechanisms:	Array of mechanisms supported by the provider, specified
 *			by the provider during registration
 * pd_sched_info:	Scheduling information associated with the provider
 * pd_mech_list_count:	The number of entries in pi_mechanisms, specified
 *			by the provider during registration
 * pd_remove_cv:	cv to wait on while the provider queue drains
 * pd_description:	Provider description string
 * pd_hash_limit	Maximum data size that hash mechanisms of this provider
 * 			can support.
 * pd_kcf_prov_handle:	KCF-private handle assigned by KCF
 * pd_prov_id:		Identification # assigned by KCF to provider
 * pd_kstat:		kstat associated with the provider
 * pd_ks_data:		kstat data
 */
typedef struct kcf_provider_desc {
	crypto_session_id_t		pd_sid;
	uint_t				pd_refcnt;
	uint_t				pd_irefcnt;
	kmutex_t			pd_lock;
	kcf_prov_state_t		pd_state;
	kcondvar_t			pd_resume_cv;
	crypto_provider_handle_t	pd_prov_handle;
	const crypto_ops_t			*pd_ops_vector;
	ushort_t			pd_mech_indx[KCF_OPS_CLASSSIZE]\
					    [KCF_MAXMECHTAB];
	const crypto_mech_info_t		*pd_mechanisms;
	kcf_sched_info_t		pd_sched_info;
	uint_t				pd_mech_list_count;
	kcondvar_t			pd_remove_cv;
	const char				*pd_description;
	uint_t				pd_hash_limit;
	crypto_kcf_provider_handle_t	pd_kcf_prov_handle;
	crypto_provider_id_t		pd_prov_id;
	kstat_t				*pd_kstat;
	kcf_prov_stats_t		pd_ks_data;
} kcf_provider_desc_t;

/* atomic operations in linux implicitly form a memory barrier */
#define	membar_exit()

/*
 * If a component has a reference to a kcf_provider_desc_t,
 * it REFHOLD()s. A new provider descriptor which is referenced only
 * by the providers table has a reference counter of one.
 */
#define	KCF_PROV_REFHOLD(desc) {		\
	atomic_add_32(&(desc)->pd_refcnt, 1);	\
	ASSERT((desc)->pd_refcnt != 0);		\
}

#define	KCF_PROV_IREFHOLD(desc) {		\
	atomic_add_32(&(desc)->pd_irefcnt, 1);	\
	ASSERT((desc)->pd_irefcnt != 0);	\
}

#define	KCF_PROV_IREFRELE(desc) {				\
	ASSERT((desc)->pd_irefcnt != 0);			\
	membar_exit();						\
	if (atomic_add_32_nv(&(desc)->pd_irefcnt, -1) == 0) {	\
		cv_broadcast(&(desc)->pd_remove_cv);		\
	}							\
}

#define	KCF_PROV_REFHELD(desc)	((desc)->pd_refcnt >= 1)

#define	KCF_PROV_REFRELE(desc) {				\
	ASSERT((desc)->pd_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(desc)->pd_refcnt, -1) == 0) {	\
		kcf_provider_zero_refcnt((desc));		\
	}							\
}


/*
 * An element in a mechanism provider descriptors chain.
 * The kcf_prov_mech_desc_t is duplicated in every chain the provider belongs
 * to. This is a small tradeoff memory vs mutex spinning time to access the
 * common provider field.
 */

typedef struct kcf_prov_mech_desc {
	struct kcf_mech_entry		*pm_me;		/* Back to the head */
	struct kcf_prov_mech_desc	*pm_next;	/* Next in the chain */
	crypto_mech_info_t		pm_mech_info;	/* Provider mech info */
	kcf_provider_desc_t		*pm_prov_desc;	/* Common desc. */
} kcf_prov_mech_desc_t;

/*
 * A mechanism entry in an xxx_mech_tab[]. me_pad was deemed
 * to be unnecessary and removed.
 */
typedef	struct kcf_mech_entry {
	crypto_mech_name_t	me_name;	/* mechanism name */
	crypto_mech_type_t	me_mechid;	/* Internal id for mechanism */
	kmutex_t		me_mutex;	/* access protection	*/
	kcf_prov_mech_desc_t	*me_sw_prov;    /* provider */
} kcf_mech_entry_t;

/*
 * If a component has a reference to a kcf_policy_desc_t,
 * it REFHOLD()s. A new policy descriptor which is referenced only
 * by the policy table has a reference count of one.
 */
#define	KCF_POLICY_REFHOLD(desc) {		\
	atomic_add_32(&(desc)->pd_refcnt, 1);	\
	ASSERT((desc)->pd_refcnt != 0);		\
}

/*
 * Releases a reference to a policy descriptor. When the last
 * reference is released, the descriptor is freed.
 */
#define	KCF_POLICY_REFRELE(desc) {				\
	ASSERT((desc)->pd_refcnt != 0);				\
	membar_exit();						\
	if (atomic_add_32_nv(&(desc)->pd_refcnt, -1) == 0)	\
		kcf_policy_free_desc(desc);			\
}

/*
 * Global tables. The sizes are from the predefined PKCS#11 v2.20 mechanisms,
 * with a margin of few extra empty entry points
 */

#define	KCF_MAXDIGEST		16	/* Digests */
#define	KCF_MAXCIPHER		64	/* Ciphers */
#define	KCF_MAXMAC		40	/* Message authentication codes */

typedef	enum {
	KCF_DIGEST_CLASS = 1,
	KCF_CIPHER_CLASS,
	KCF_MAC_CLASS,
} kcf_ops_class_t;

#define	KCF_FIRST_OPSCLASS	KCF_DIGEST_CLASS
#define	KCF_LAST_OPSCLASS	KCF_MAC_CLASS

/* The table of all the kcf_xxx_mech_tab[]s, indexed by kcf_ops_class */

typedef	struct kcf_mech_entry_tab {
	int			met_size;	/* Size of the met_tab[] */
	kcf_mech_entry_t	*met_tab;	/* the table		 */
} kcf_mech_entry_tab_t;

extern const kcf_mech_entry_tab_t kcf_mech_tabs_tab[];

#define	KCF_MECHID(class, index)				\
	(((crypto_mech_type_t)(class) << 32) | (crypto_mech_type_t)(index))

#define	KCF_MECH2CLASS(mech_type) ((kcf_ops_class_t)((mech_type) >> 32))

#define	KCF_MECH2INDEX(mech_type) ((int)(mech_type))

#define	KCF_TO_PROV_MECH_INDX(pd, mech_type) 			\
	((pd)->pd_mech_indx[KCF_MECH2CLASS(mech_type)] 		\
	[KCF_MECH2INDEX(mech_type)])

#define	KCF_TO_PROV_MECHINFO(pd, mech_type)			\
	((pd)->pd_mechanisms[KCF_TO_PROV_MECH_INDX(pd, mech_type)])

#define	KCF_TO_PROV_MECHNUM(pd, mech_type)			\
	(KCF_TO_PROV_MECHINFO(pd, mech_type).cm_mech_number)

#define	KCF_CAN_SHARE_OPSTATE(pd, mech_type)			\
	((KCF_TO_PROV_MECHINFO(pd, mech_type).cm_mech_flags) &	\
	CRYPTO_CAN_SHARE_OPSTATE)

/* ps_refcnt is protected by cm_lock in the crypto_minor structure */
typedef struct crypto_provider_session {
	struct crypto_provider_session *ps_next;
	crypto_session_id_t		ps_session;
	kcf_provider_desc_t		*ps_provider;
	kcf_provider_desc_t		*ps_real_provider;
	uint_t				ps_refcnt;
} crypto_provider_session_t;

typedef struct crypto_session_data {
	kmutex_t			sd_lock;
	kcondvar_t			sd_cv;
	uint32_t			sd_flags;
	int				sd_pre_approved_amount;
	crypto_ctx_t			*sd_digest_ctx;
	crypto_ctx_t			*sd_encr_ctx;
	crypto_ctx_t			*sd_decr_ctx;
	crypto_ctx_t			*sd_sign_ctx;
	crypto_ctx_t			*sd_verify_ctx;
	crypto_ctx_t			*sd_sign_recover_ctx;
	crypto_ctx_t			*sd_verify_recover_ctx;
	kcf_provider_desc_t		*sd_provider;
	void				*sd_find_init_cookie;
	crypto_provider_session_t	*sd_provider_session;
} crypto_session_data_t;

#define	CRYPTO_SESSION_IN_USE		0x00000001
#define	CRYPTO_SESSION_IS_BUSY		0x00000002
#define	CRYPTO_SESSION_IS_CLOSED	0x00000004

#define	KCF_MAX_PIN_LEN			1024

/*
 * Per-minor info.
 *
 * cm_lock protects everything in this structure except for cm_refcnt.
 */
typedef struct crypto_minor {
	uint_t				cm_refcnt;
	kmutex_t			cm_lock;
	kcondvar_t			cm_cv;
	crypto_session_data_t		**cm_session_table;
	uint_t				cm_session_table_count;
	kcf_provider_desc_t		**cm_provider_array;
	uint_t				cm_provider_count;
	crypto_provider_session_t	*cm_provider_session;
} crypto_minor_t;

/*
 * Return codes for internal functions
 */
#define	KCF_SUCCESS		0x0	/* Successful call */
#define	KCF_INVALID_MECH_NUMBER	0x1	/* invalid mechanism number */
#define	KCF_INVALID_MECH_NAME	0x2	/* invalid mechanism name */
#define	KCF_INVALID_MECH_CLASS	0x3	/* invalid mechanism class */
#define	KCF_MECH_TAB_FULL	0x4	/* Need more room in the mech tabs. */
#define	KCF_INVALID_INDX	((ushort_t)-1)

/*
 * Wrappers for ops vectors. In the wrapper definitions below, the pd
 * argument always corresponds to a pointer to a provider descriptor
 * of type kcf_prov_desc_t.
 */

#define	KCF_PROV_DIGEST_OPS(pd)		((pd)->pd_ops_vector->co_digest_ops)
#define	KCF_PROV_CIPHER_OPS(pd)		((pd)->pd_ops_vector->co_cipher_ops)
#define	KCF_PROV_MAC_OPS(pd)		((pd)->pd_ops_vector->co_mac_ops)
#define	KCF_PROV_CTX_OPS(pd)		((pd)->pd_ops_vector->co_ctx_ops)

/*
 * Wrappers for crypto_digest_ops(9S) entry points.
 */

#define	KCF_PROV_DIGEST_INIT(pd, ctx, mech, req) ( \
	(KCF_PROV_DIGEST_OPS(pd) && KCF_PROV_DIGEST_OPS(pd)->digest_init) ? \
	KCF_PROV_DIGEST_OPS(pd)->digest_init(ctx, mech, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_cipher_ops(9S) entry points.
 */

#define	KCF_PROV_ENCRYPT_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt_init) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt_init(ctx, mech, key, template, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_ENCRYPT_ATOMIC(pd, session, mech, key, plaintext, ciphertext, \
	    template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->encrypt_atomic) ? \
	KCF_PROV_CIPHER_OPS(pd)->encrypt_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, plaintext, ciphertext, \
	    template, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_DECRYPT_ATOMIC(pd, session, mech, key, ciphertext, plaintext, \
	    template, req) ( \
	(KCF_PROV_CIPHER_OPS(pd) && KCF_PROV_CIPHER_OPS(pd)->decrypt_atomic) ? \
	KCF_PROV_CIPHER_OPS(pd)->decrypt_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, ciphertext, plaintext, \
	    template, req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_mac_ops(9S) entry points.
 */

#define	KCF_PROV_MAC_INIT(pd, ctx, mech, key, template, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_init) ? \
	KCF_PROV_MAC_OPS(pd)->mac_init(ctx, mech, key, template, req) \
	: CRYPTO_NOT_SUPPORTED)

/*
 * The _ (underscore) in _mac is needed to avoid replacing the
 * function mac().
 */
#define	KCF_PROV_MAC_UPDATE(pd, ctx, data, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_update) ? \
	KCF_PROV_MAC_OPS(pd)->mac_update(ctx, data, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_FINAL(pd, ctx, mac, req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_final) ? \
	KCF_PROV_MAC_OPS(pd)->mac_final(ctx, mac, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_MAC_ATOMIC(pd, session, mech, key, data, mac, template, \
	    req) ( \
	(KCF_PROV_MAC_OPS(pd) && KCF_PROV_MAC_OPS(pd)->mac_atomic) ? \
	KCF_PROV_MAC_OPS(pd)->mac_atomic( \
	    (pd)->pd_prov_handle, session, mech, key, data, mac, template, \
	    req) : \
	CRYPTO_NOT_SUPPORTED)

/*
 * Wrappers for crypto_ctx_ops(9S) entry points.
 */

#define	KCF_PROV_CREATE_CTX_TEMPLATE(pd, mech, key, template, size, req) ( \
	(KCF_PROV_CTX_OPS(pd) && KCF_PROV_CTX_OPS(pd)->create_ctx_template) ? \
	KCF_PROV_CTX_OPS(pd)->create_ctx_template( \
	    (pd)->pd_prov_handle, mech, key, template, size, req) : \
	CRYPTO_NOT_SUPPORTED)

#define	KCF_PROV_FREE_CONTEXT(pd, ctx) ( \
	(KCF_PROV_CTX_OPS(pd) && KCF_PROV_CTX_OPS(pd)->free_context) ? \
	KCF_PROV_CTX_OPS(pd)->free_context(ctx) : CRYPTO_NOT_SUPPORTED)


/* Miscellaneous */
extern void kcf_destroy_mech_tabs(void);
extern void kcf_init_mech_tabs(void);
extern int kcf_add_mech_provider(short, kcf_provider_desc_t *,
    kcf_prov_mech_desc_t **);
extern void kcf_remove_mech_provider(const char *, kcf_provider_desc_t *);
extern int kcf_get_mech_entry(crypto_mech_type_t, kcf_mech_entry_t **);
extern kcf_provider_desc_t *kcf_alloc_provider_desc(void);
extern void kcf_provider_zero_refcnt(kcf_provider_desc_t *);
extern void kcf_free_provider_desc(kcf_provider_desc_t *);
extern void undo_register_provider(kcf_provider_desc_t *, boolean_t);
extern int crypto_uio_data(crypto_data_t *, uchar_t *, int, cmd_type_t,
    void *, void (*update)(void));
extern int crypto_put_output_data(uchar_t *, crypto_data_t *, int);
extern int crypto_update_iov(void *, crypto_data_t *, crypto_data_t *,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *));
extern int crypto_update_uio(void *, crypto_data_t *, crypto_data_t *,
    int (*cipher)(void *, caddr_t, size_t, crypto_data_t *),
    void (*copy_block)(uint8_t *, uint64_t *));

/* Access to the provider's table */
extern void kcf_prov_tab_destroy(void);
extern void kcf_prov_tab_init(void);
extern int kcf_prov_tab_add_provider(kcf_provider_desc_t *);
extern int kcf_prov_tab_rem_provider(crypto_provider_id_t);
extern kcf_provider_desc_t *kcf_prov_tab_lookup(crypto_provider_id_t);
extern int kcf_get_sw_prov(crypto_mech_type_t, kcf_provider_desc_t **,
    kcf_mech_entry_t **, boolean_t);


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_IMPL_H */
