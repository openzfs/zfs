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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_ZIL_IMPL_H
#define	_SYS_ZIL_IMPL_H

#include <sys/zil.h>
#include <sys/dmu_objset.h>
#include <sys/zfeature.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Intent log transaction lists
 */
typedef struct itxs {
	list_t		i_sync_list;	/* list of synchronous itxs */
	avl_tree_t	i_async_tree;	/* tree of foids for async itxs */
} itxs_t;

typedef struct itxg {
	kmutex_t	itxg_lock;	/* lock for this structure */
	uint64_t	itxg_txg;	/* txg for this chain */
	itxs_t		*itxg_itxs;	/* sync and async itxs */
} itxg_t;

/* for async nodes we build up an AVL tree of lists of async itxs per file */
typedef struct itx_async_node {
	uint64_t	ia_foid;	/* file object id */
	list_t		ia_list;	/* list of async itxs for this foid */
	avl_node_t	ia_node;	/* AVL tree linkage */
} itx_async_node_t;

typedef struct zil_vtable {
	size_t zlvt_alloc_size;

	/* static methods */

	void (*zlvt_init)(void);
	void (*zlvt_fini)(void);
	int (*zlvt_reset_logs)(spa_t *);

	void (*zlvt_init_header)(void *zh, size_t size);
	boolean_t (*zlvt_validate_header_format)(const void *zh, size_t size);

	/* methods */

	void (*zlvt_ctor)(zilog_t *zilog);
	void (*zlvt_dtor)(zilog_t *zilog);

	uint64_t (*zlvt_max_copied_data)(zilog_t *zilog);

	void (*zlvt_commit)(zilog_t *zilog, uint64_t foid);
	void (*zlvt_commit_on_spa_not_writeable)(zilog_t *zilog);

	void (*zlvt_destroy)(zilog_t *zilog);
	void (*zlvt_destroy_sync)(zilog_t *zilog, dmu_tx_t *tx);

	void (*zlvt_sync)(zilog_t *zilog, dmu_tx_t *tx);

	void (*zlvt_open)(zilog_t *zilog);
	void (*zlvt_close)(zilog_t *zilog);

	void (*zlvt_replay)(zilog_t *zilog, objset_t *os, void *arg,
	    zil_replay_func_t *replay_func[TX_MAX_TYPE]);
	boolean_t (*zlvt_replaying)(zilog_t *zilog, dmu_tx_t *tx);
    boolean_t (*zlvt_get_is_replaying_no_sideffects)(zilog_t *zilog);

	int (*zlvt_check_log_chain)(zilog_t *zilog);
	boolean_t (*zlvt_is_claimed)(zilog_t *zilog);
	int (*zlvt_claim)(zilog_t *zilog, dmu_tx_t *tx);
	int (*zlvt_clear)(zilog_t *zilog, dmu_tx_t *tx);

} zil_vtable_t;

extern const zil_vtable_t zillwb_vtable;

typedef const zil_vtable_t *zil_const_zil_vtable_ptr_t;
extern const zil_const_zil_vtable_ptr_t zil_vtables[ZIL_KIND_COUNT];

static inline __attribute__((always_inline))
boolean_t
zil_is_valid_zil_kind(uint64_t zil_kind)
{
	boolean_t invalid;
	const char *zil_kind_str = zil_kind_to_str(zil_kind, &invalid);
	if (invalid) {
		zfs_dbgmsg("zil_kind=%llu (%s) ZIL_KIND_COUNT=%d",
		    (u_longlong_t)zil_kind,
		    zil_kind_str, ZIL_KIND_COUNT);
	}
	return (!invalid);
}

static inline __attribute((always_inline))
const zil_vtable_t *
zil_vtable_for_kind(uint64_t zil_kind)
{
	VERIFY(zil_is_valid_zil_kind(zil_kind));
	return (zil_vtables[zil_kind]);
}

static inline
int
zil_kind_specific_data_from_header(spa_t *spa, const zil_header_t *zh, const void **zhk_out, size_t *size_out, zil_vtable_t const **vtable_out, zh_kind_t *zk_out)
{
	ASSERT(zh);

	const zil_vtable_t *vt;
	zh_kind_t zk;
	const void *zhk;
	size_t size;

	if (!spa_feature_is_active(spa, SPA_FEATURE_ZIL_KINDS)) {
		zk = ZIL_KIND_LWB;
		zhk = &zh->zh_v1.zhv1_lwb;
		size = sizeof(zil_header_lwb_t);
		goto okout;
	}

	zk = zh->zh_v2.zh_kind;
	switch (zh->zh_v2.zh_kind) {
		case ZIL_KIND_LWB:
			zhk = &zh->zh_v2.zh_lwb;
			size = sizeof(zh->zh_v2.zh_lwb);
			goto okout;
		default:
			/* ZIL_KIND_COUNT for grepping */
			zfs_dbgmsg("unknown zil kind %llu",
			    (u_longlong_t)zh->zh_v2.zh_kind);
			return SET_ERROR(EINVAL);
	}

okout:
	vt = zil_vtable_for_kind(zk);
	VERIFY(vt);

	if (vtable_out)
		*vtable_out = vt;
	if (zk_out)
		*zk_out = zk;
	if (size_out)
		*size_out = size;
	if (zhk_out)
		*zhk_out = zhk;
	return (0);
}


/*
 * Stable storage intent log management structure.  One per dataset.
 */

struct zilog {
	const zil_vtable_t	*zl_vtable;

	struct dsl_pool	*zl_dmu_pool;	/* DSL pool */
	spa_t		*zl_spa;	/* handle for read/write log */
	const zil_header_t *zl_header;	/* log header buffer */
	objset_t	*zl_os;		/* object set we're logging */
	zil_get_data_t	*zl_get_data;	/* callback to get object content */

	uint8_t		zl_logbias;	/* latency or throughput */
	uint8_t		zl_sync;	/* synchronous or asynchronous */
	itxg_t		zl_itxg[TXG_SIZE]; /* intent log txg chains */

	txg_node_t	zl_dirty_link;	/* protected by dp_dirty_zilogs list */
	uint64_t	zl_dirty_max_txg; /* highest txg used to dirty zilog */

};

void zil_fill_commit_list(zilog_t *zilog, list_t *commit_list);
void zil_async_to_sync(zilog_t *zilog, uint64_t foid);
boolean_t zilog_is_dirty(zilog_t *zilog);

/* zfs_log.c */
extern uint64_t	zil_max_copied_data(zilog_t *zilog);
extern void zil_itx_ctor_on_zeroed_memory(itx_t *itx, lr_t *lr, uint64_t txtype, size_t lrsize);

static inline boolean_t
zil_itx_is_write_need_copy(const itx_t *itx)
{
	/* short-circuiting effect is important for correctness! */
	return (itx->itx_lr.lrc_txtype == TX_WRITE &&
	    itx->itx_wr_state == WR_NEED_COPY);
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIL_IMPL_H */
