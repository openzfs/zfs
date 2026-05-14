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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright (c) 2024, Klara, Inc.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_SYS_ZAP_IMPL_H
#define	_SYS_ZAP_IMPL_H

#include <sys/zap.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int fzap_default_block_shift;

#define	ZAP_MAGIC 0x2F52AB2ABULL

#define	FZAP_BLOCK_SHIFT(zap)	((zap)->zap_f.zap_block_shift)

#define	MZAP_ENT_LEN		64
#define	MZAP_NAME_LEN		(MZAP_ENT_LEN - 8 - 4 - 2)

#define	ZAP_NEED_CD		(-1U)

typedef struct mzap_ent_phys {
	uint64_t mze_value;
	uint32_t mze_cd;
	uint16_t mze_pad;	/* in case we want to chain them someday */
	char mze_name[MZAP_NAME_LEN];
} mzap_ent_phys_t;

typedef struct mzap_phys {
	uint64_t mz_block_type;	/* ZBT_MICRO */
	uint64_t mz_salt;
	uint64_t mz_normflags;
	uint64_t mz_pad[5];
	mzap_ent_phys_t mz_chunk[1];
	/* actually variable size depending on block size */
} mzap_phys_t;

typedef struct mzap_ent {
	uint32_t mze_hash;
	uint16_t mze_cd; /* copy from mze_phys->mze_cd */
	uint16_t mze_chunkid;
} mzap_ent_t;

#define	MZE_PHYS(zap, mze) \
	(&zap_m_phys(zap)->mz_chunk[(mze)->mze_chunkid])

/*
 * The (fat) zap is stored in one object. It is an array of
 * 1<<FZAP_BLOCK_SHIFT byte blocks. The layout looks like one of:
 *
 * ptrtbl fits in first block:
 * 	[zap_phys_t zap_ptrtbl_shift < 6] [zap_leaf_t] ...
 *
 * ptrtbl too big for first block:
 * 	[zap_phys_t zap_ptrtbl_shift >= 6] [zap_leaf_t] [ptrtbl] ...
 *
 */

struct dmu_buf;
struct zap_leaf;

#define	ZBT_LEAF		((1ULL << 63) + 0)
#define	ZBT_HEADER		((1ULL << 63) + 1)
#define	ZBT_MICRO		((1ULL << 63) + 3)
/* any other values are ptrtbl blocks */

/*
 * the embedded pointer table takes up half a block:
 * block size / entry size (2^3) / 2
 */
#define	ZAP_EMBEDDED_PTRTBL_SHIFT(zap) (FZAP_BLOCK_SHIFT(zap) - 3 - 1)

/*
 * The embedded pointer table starts half-way through the block.  Since
 * the pointer table itself is half the block, it starts at (64-bit)
 * word number (1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap)).
 */
#define	ZAP_EMBEDDED_PTRTBL_ENT(zap, idx) \
	((uint64_t *)zap_f_phys(zap)) \
	[(idx) + (1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap))]

/*
 * TAKE NOTE:
 * If zap_phys_t is modified, zap_byteswap() must be modified.
 */
typedef struct zap_phys {
	uint64_t zap_block_type;	/* ZBT_HEADER */
	uint64_t zap_magic;		/* ZAP_MAGIC */

	struct zap_table_phys {
		uint64_t zt_blk;	/* starting block number */
		uint64_t zt_numblks;	/* number of blocks */
		uint64_t zt_shift;	/* bits to index it */
		uint64_t zt_nextblk;	/* next (larger) copy start block */
		uint64_t zt_blks_copied; /* number source blocks copied */
	} zap_ptrtbl;

	uint64_t zap_freeblk;		/* the next free block */
	uint64_t zap_num_leafs;		/* number of leafs */
	uint64_t zap_num_entries;	/* number of entries */
	uint64_t zap_salt;		/* salt to stir into hash function */
	uint64_t zap_normflags;		/* flags for u8_textprep_str() */
	uint64_t zap_flags;		/* zap_flags_t */
	/*
	 * This structure is followed by padding, and then the embedded
	 * pointer table.  The embedded pointer table takes up second
	 * half of the block.  It is accessed using the
	 * ZAP_EMBEDDED_PTRTBL_ENT() macro.
	 */
} zap_phys_t;

typedef struct zap_table_phys zap_table_phys_t;

typedef struct zap {
	dmu_buf_user_t zap_dbu;
	objset_t *zap_objset;
	uint64_t zap_object;
	dnode_t *zap_dnode;
	struct dmu_buf *zap_dbuf;
	krwlock_t zap_rwlock;
	boolean_t zap_ismicro;
	int zap_normflags;
	uint64_t zap_salt;
	union {
		struct {
			/*
			 * zap_num_entries_mtx protects
			 * zap_num_entries
			 */
			kmutex_t zap_num_entries_mtx;
			int zap_block_shift;
		} zap_fat;
		struct {
			int16_t zap_num_entries;
			int16_t zap_num_chunks;
			int16_t zap_alloc_next;
			zfs_btree_t zap_tree;
		} zap_micro;
	} zap_u;
} zap_t;

#define	zap_f	zap_u.zap_fat
#define	zap_m	zap_u.zap_micro

static inline zap_phys_t *
zap_f_phys(zap_t *zap)
{
	return (zap->zap_dbuf->db_data);
}

static inline mzap_phys_t *
zap_m_phys(zap_t *zap)
{
	return (zap->zap_dbuf->db_data);
}

/*
 * zap_name_t carries the original key and whatever we've derived from it
 * (normalised form, hash, etc) as we work through completing the operation.
 */
typedef struct zap_name {
	zap_t *zn_zap;
	int zn_key_intlen;
	const void *zn_key_orig;
	int zn_key_orig_numints;
	const void *zn_key_norm;
	int zn_key_norm_numints;
	uint64_t zn_hash;
	matchtype_t zn_matchtype;
	int zn_normflags;
	int zn_normbuf_len;
	char zn_normbuf[];
} zap_name_t;

/*
 * Allocate a zap_name_t. The longname flag ensures there is enough room to
 * hold a long filename when the 'longname' pool feature is active.
 */
zap_name_t *zap_name_alloc(zap_t *zap, boolean_t longname);

/*
 * Allocate a zap_name_t for the given key. zap_name_init_str() will be
 * called to normalise the key and initialise the struct.
 */
zap_name_t *zap_name_alloc_str(zap_t *zap, const char *key, matchtype_t mt);

/*
 * Allocate a zap_name_t for a uint64 array key.
 */
zap_name_t *zap_name_alloc_uint64(zap_t *zap, const uint64_t *key, int numints);

/*
 * Free a zap_name_t.
 */
void zap_name_free(zap_name_t *zn);

/*
 * Initialise an existing zap_name_t with the normalised form of the key,
 * computed according to the given matchtype.
 */
int zap_name_init_str(zap_name_t *zn, const char *key, matchtype_t mt);

/*
 * Compare 'matchname' with the name represented by the zap_name_t, applying
 * the same normalisation method first. Returns true if the normalised forms
 * match, false otherwise.
 */
boolean_t zap_match(zap_name_t *zn, const char *matchname);

/*
 * Compute and return the 64-bit hash for the name, according to the name
 * type and hash flags.
 */
uint64_t zap_hash(zap_name_t *zn);

/*
 * Return a zap_t for the given on-disk object, locked and ready for use.
 * The zap_t will be allocated and loaded from disk if its not already loaded.
 */
int zap_lock(objset_t *os, uint64_t obj, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, const void *tag,
    zap_t **zapp);
int zap_lock_by_dnode(dnode_t *dn, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, const void *tag,
    zap_t **zapp);

/* Unlock and release a zap_t. */
void zap_unlock(zap_t *zap, const void *tag);

/*
 * Try to upgrade a zap lock from READER to WRITER. If the upgrade is not
 * possible without blocking, returns 0. If the upgrade happened, returns 1.
 */
int zap_lock_try_upgrade(zap_t *zap, dmu_tx_t *tx);

/*
 * Upgrade a zap lock from READER to WRITER. If it can't be upgraded
 * immediately it will block.
 */
void zap_lock_upgrade(zap_t *zap, dmu_tx_t *tx);

/* zap_t release function for when associated dbuf is evicted. */
void zap_evict_sync(void *dbu);

/* Misc internal state & config. */
int zap_hashbits(zap_t *zap);
uint32_t zap_maxcd(zap_t *zap);
uint64_t zap_getflags(zap_t *zap);

/* Microzap implementation. */
zap_t *mzap_open(dmu_buf_t *db);
int mzap_upgrade(zap_t **zapp, dmu_tx_t *tx, zap_flags_t flags);
mzap_ent_t *mze_find(zap_name_t *zn, zfs_btree_index_t *idx);
boolean_t mze_canfit_fzap_leaf(zap_name_t *zn, uint64_t hash);
void mze_destroy(zap_t *zap);
boolean_t mzap_normalization_conflict(zap_t *zap, zap_name_t *zn,
    mzap_ent_t *mze, zfs_btree_index_t *idx);
void mzap_addent(zap_name_t *zn, uint64_t value);
void mzap_byteswap(mzap_phys_t *buf, size_t size);
uint64_t zap_get_micro_max_size(spa_t *spa);

/* Fatzap implementation. */
void fzap_byteswap(void *buf, size_t size);
int fzap_count(zap_t *zap, uint64_t *count);
int fzap_lookup(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    char *realname, int rn_len, boolean_t *normalization_conflictp,
    uint64_t *actual_num_integers);
void fzap_prefetch(zap_name_t *zn);
int fzap_add(zap_name_t *zn, uint64_t integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);
int fzap_update(zap_name_t *zn, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);
int fzap_length(zap_name_t *zn,
    uint64_t *integer_size, uint64_t *num_integers);
int fzap_remove(zap_name_t *zn, dmu_tx_t *tx);
int fzap_cursor_retrieve(zap_t *zap, zap_cursor_t *zc, zap_attribute_t *za);
void fzap_get_stats(zap_t *zap, zap_stats_t *zs);
void zap_put_leaf(struct zap_leaf *l);
int fzap_add_cd(zap_name_t *zn, uint64_t integer_size, uint64_t num_integers,
    const void *val, uint32_t cd, dmu_tx_t *tx);
void fzap_upgrade(zap_t *zap, dmu_tx_t *tx, zap_flags_t flags);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZAP_IMPL_H */
