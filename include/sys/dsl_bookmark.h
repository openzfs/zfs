/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DSL_BOOKMARK_H
#define	_SYS_DSL_BOOKMARK_H

#include <sys/zfs_context.h>
#include <sys/zfs_refcount.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * On disk zap object.
 */
typedef struct zfs_bookmark_phys {
	uint64_t zbm_guid;		/* guid of bookmarked dataset */
	uint64_t zbm_creation_txg;	/* birth transaction group */
	uint64_t zbm_creation_time;	/* bookmark creation time */

	/* fields used for redacted send / recv */
	uint64_t zbm_redaction_obj;	/* redaction list object */
	uint64_t zbm_flags;		/* ZBM_FLAG_* */

	/* fields used for bookmark written size */
	uint64_t zbm_referenced_bytes_refd;
	uint64_t zbm_compressed_bytes_refd;
	uint64_t zbm_uncompressed_bytes_refd;
	uint64_t zbm_referenced_freed_before_next_snap;
	uint64_t zbm_compressed_freed_before_next_snap;
	uint64_t zbm_uncompressed_freed_before_next_snap;

	/* fields used for raw sends */
	uint64_t zbm_ivset_guid;
} zfs_bookmark_phys_t;


#define	BOOKMARK_PHYS_SIZE_V1	(3 * sizeof (uint64_t))
#define	BOOKMARK_PHYS_SIZE_V2	(12 * sizeof (uint64_t))

typedef enum zbm_flags {
	ZBM_FLAG_HAS_FBN = (1 << 0),
	ZBM_FLAG_SNAPSHOT_EXISTS = (1 << 1),
} zbm_flags_t;

typedef struct redaction_list_phys {
	uint64_t rlp_last_object;
	uint64_t rlp_last_blkid;
	uint64_t rlp_num_entries;
	uint64_t rlp_num_snaps;
	uint64_t rlp_snaps[]; /* variable length */
} redaction_list_phys_t;

typedef struct redaction_list {
	dmu_buf_user_t		rl_dbu;
	redaction_list_phys_t	*rl_phys;
	dmu_buf_t		*rl_dbuf;
	uint64_t		rl_object;
	zfs_refcount_t		rl_longholds;
	objset_t		*rl_mos;
} redaction_list_t;

/* node in ds_bookmarks */
typedef struct dsl_bookmark_node {
	char *dbn_name; /* free with strfree() */
	kmutex_t dbn_lock; /* protects dirty/phys in block_killed */
	boolean_t dbn_dirty; /* in currently syncing txg */
	zfs_bookmark_phys_t dbn_phys;
	avl_node_t dbn_node;
} dsl_bookmark_node_t;

typedef struct redact_block_phys {
	uint64_t	rbp_object;
	uint64_t	rbp_blkid;
	/*
	 * The top 16 bits of this field represent the block size in sectors of
	 * the blocks in question; the bottom 48 bits are used to store the
	 * number of consecutive blocks that are in the redaction list.  They
	 * should be accessed using the inline functions below.
	 */
	uint64_t	rbp_size_count;
	uint64_t	rbp_padding;
} redact_block_phys_t;

typedef int (*rl_traverse_callback_t)(redact_block_phys_t *, void *);


typedef struct dsl_bookmark_create_arg {
	nvlist_t *dbca_bmarks;
	nvlist_t *dbca_errors;
} dsl_bookmark_create_arg_t;

typedef struct dsl_bookmark_create_redacted_arg {
	const char	*dbcra_bmark;
	const char	*dbcra_snap;
	redaction_list_t **dbcra_rl;
	uint64_t	dbcra_numsnaps;
	uint64_t	*dbcra_snaps;
	void		*dbcra_tag;
} dsl_bookmark_create_redacted_arg_t;

int dsl_bookmark_create(nvlist_t *, nvlist_t *);
int dsl_bookmark_create_nvl_validate(nvlist_t *);
int dsl_bookmark_create_check(void *arg, dmu_tx_t *tx);
void dsl_bookmark_create_sync(void *arg, dmu_tx_t *tx);
int dsl_bookmark_create_redacted(const char *, const char *, uint64_t,
    uint64_t *, void *, redaction_list_t **);
int dsl_get_bookmarks(const char *, nvlist_t *, nvlist_t *);
int dsl_get_bookmarks_impl(dsl_dataset_t *, nvlist_t *, nvlist_t *);
int dsl_get_bookmark_props(const char *, const char *, nvlist_t *);
int dsl_bookmark_destroy(nvlist_t *, nvlist_t *);
int dsl_bookmark_lookup(struct dsl_pool *, const char *,
    struct dsl_dataset *, zfs_bookmark_phys_t *);
int dsl_bookmark_lookup_impl(dsl_dataset_t *, const char *,
    zfs_bookmark_phys_t *);
int dsl_redaction_list_hold_obj(struct dsl_pool *, uint64_t, void *,
    redaction_list_t **);
void dsl_redaction_list_rele(redaction_list_t *, void *);
void dsl_redaction_list_long_hold(struct dsl_pool *, redaction_list_t *,
    void *);
void dsl_redaction_list_long_rele(redaction_list_t *, void *);
boolean_t dsl_redaction_list_long_held(redaction_list_t *);
int dsl_bookmark_init_ds(dsl_dataset_t *);
void dsl_bookmark_fini_ds(dsl_dataset_t *);
boolean_t dsl_bookmark_ds_destroyed(dsl_dataset_t *, dmu_tx_t *);
void dsl_bookmark_snapshotted(dsl_dataset_t *, dmu_tx_t *);
void dsl_bookmark_block_killed(dsl_dataset_t *, const blkptr_t *, dmu_tx_t *);
void dsl_bookmark_sync_done(dsl_dataset_t *, dmu_tx_t *);
void dsl_bookmark_node_add(dsl_dataset_t *, dsl_bookmark_node_t *, dmu_tx_t *);
uint64_t dsl_bookmark_latest_txg(dsl_dataset_t *);
int dsl_redaction_list_traverse(redaction_list_t *, zbookmark_phys_t *,
    rl_traverse_callback_t, void *);
void dsl_bookmark_next_changed(dsl_dataset_t *, dsl_dataset_t *, dmu_tx_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_BOOKMARK_H */
